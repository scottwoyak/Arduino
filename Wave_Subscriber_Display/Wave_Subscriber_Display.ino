//
// Wave Subscriber Display
//
// Subscribes to live lake wave-height telemetry over a WebSocket connection and renders
// a smoothed rolling bar chart on the display.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   receives live wave sensor readings as they arrive.
// - Applies short-term smoothing/buffering before updating the displayed value and chart.
// - Resets the device on telemetry disconnect or error.
//

// Undefine to use the remote server.
//#define TELEMETRY_LOCAL

constexpr auto TELEMETRY_TOPIC = "Waves/LakeP";

#include <iomanip>
#include <sstream>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_NEOPIXEL_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Feather ESP32-S3 or Waveshare ESP32-S3-Zero)."
#endif
#ifndef ARDUINO_BUILTIN_LED_SUPPORTED
#error "This sketch requires a board with a separate built-in LED (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "BarChart.h"
#include "MovingBarChart.h"
#include "BufferedTimeSeries.h"
#include "RollingRate.h"
#include "RollingStats.h"
#include "SerialX.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "Util.h"
#include "WiFiSettings.h"

// ----------- Telemetry
Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);
RollingRate displayRate(100);
RollingRate serverRate(100);
RollingStats sensorReadings(500);
std::string receivedValues;
uint32_t receivedValueCount = 0;
uint32_t rejectedSensorValueCount = 0;
bool newValueReceived = false;

// ----------- Built-in LED (flashes on each received telemetry value)
constexpr uint16_t RECEIVE_LED_FLASH_MS = 20;

// ----------- Time Intervals

// How often summary stats (server/display rates, received/rejected value lists) are
// printed to the serial log.
constexpr unsigned long LOG_INTERVAL_MS = 5000;

// Duration of history retained in the waveHeight buffer. waveHeight.get() interpolates
// a value at the midpoint of this window (i.e. roughly BUFFER_TIME_SPAN_MS / 2 behind
// "now"), so this also determines the display/chart lag.
constexpr unsigned long BUFFER_TIME_SPAN_MS = 2000;

// Expected sample spacing, used only to size the waveHeight buffer's interpolation
// resolution. Set to support telemetry arriving at up to 30 samples/sec; readings are
// processed as soon as they arrive rather than on a fixed poll interval.
constexpr unsigned long BUFFER_RESOLUTION_MS = 33;

// How often the rolling bar chart is redrawn on the display.
constexpr unsigned long CHART_UPDATE_MS = 30;

// Maximum plausible rate of depth change (cm/sec); larger rates of change are
// glitches/dropouts and are rejected so a single bad reading doesn't spike the baseline
// average or the chart. Derived from a 6 cm max jump at the ~5 samples/sec telemetry rate.
constexpr float MAX_RATE_CM_PER_SEC = 30;

BufferedTimeSeries waveHeight(BUFFER_TIME_SPAN_MS, BUFFER_RESOLUTION_MS);
Timer logTimer(LOG_INTERVAL_MS);
Timer chartTimer(CHART_UPDATE_MS);

Format heightFormat("###.# cm", Format::Alignment::RIGHT);

// ----------- Display layout
constexpr uint16_t DISPLAY_HEIGHT = 135;
constexpr uint16_t DISPLAY_WIDTH = 240;
constexpr uint16_t HEADER_HEIGHT = 2 * 8 + 4; // one line of text size 2 plus padding
constexpr uint16_t SUBHEADING_HEIGHT = 2 * 8 + 2; // one line of text size 2 plus padding

// ----------- Rolling bar chart view
constexpr Color LakeBlue = Color565::fromRGB(0, 0, 255);

// Maximum expected magnitude of the wave height in either direction. The chart itself
// has no zero-baseline concept (bars always fill from their range minimum upward), so
// the chart is given a range of 0..2*WAVE_HEIGHT_MAX and values are shifted by
// +WAVE_HEIGHT_MAX before being plotted so that zero renders in the middle.
constexpr float WAVE_HEIGHT_MAX = 15;
constexpr Rect16 ROLLING_RECT(0, HEADER_HEIGHT + SUBHEADING_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT - SUBHEADING_HEIGHT);
MovingBarChart waterLevelChart(ROLLING_RECT, RangeF(0, 2*WAVE_HEIGHT_MAX), LakeBlue, Color::BLACK);

///
/// <summary>
/// Draws the sketch's title header (the telemetry topic) at the top of the display.
/// </summary>
///
void displayHeader()
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(2);
   arduino.println(TELEMETRY_TOPIC, Color::HEADING);
}

///
/// <summary>
/// Draws a subheading below the header indicating whether the telemetry server is
/// local or remote.
/// </summary>
///
void displaySubheading()
{
   arduino.setTextSize(2);
#ifdef TELEMETRY_LOCAL
   arduino.println("Local", Color::SUB_HEADING);
#else
   arduino.println("Remote", Color::SUB_HEADING);
#endif
}

///
/// <summary>
/// Draws the server mode (Local/Remote) and telemetry topic as footer text at the
/// bottom of the display, left and right aligned respectively. Only shown on the
/// setup screen; it's cleared when the main display is drawn on telemetry start.
/// </summary>
///
void displayFooter()
{
   Point16 savedCursor = arduino.getCursor();
   uint8_t savedTextSize = arduino.getTextSize();

   arduino.setTextSize(2);

   arduino.setCursor(0, -arduino.charH());
#ifdef TELEMETRY_LOCAL
   arduino.print("Local", Color::GRAY);
#else
   arduino.print("Remote", Color::GRAY);
#endif

   arduino.setCursor(arduino.width(), -arduino.charH());
   arduino.printR(TELEMETRY_TOPIC, Color::GRAY);

   arduino.setTextSize(savedTextSize);
   arduino.setCursor(savedCursor);
}

///
/// <summary>
/// Handles telemetry lifecycle events for this sketch: clears the display once
/// started, and resets the device on disconnect or error.
/// </summary>
///
class WaveTelemetryHandler : public TelemetryEventHandler
{
public:
   explicit WaveTelemetryHandler(IStatus* status) : TelemetryEventHandler(status, &arduino)
   {
      // suppress the repeated "get" polling requests and value echoes from the serial log
      setEchoEnabled(false);
   }

   void onStarted() override
   {
      TelemetryEventHandler::onStarted();
      arduino.clearDisplay();
      displayHeader();
      displaySubheading();
   }

   void onReceiveText(const std::string& text) override
   {
      serverRate.tick();
      newValueReceived = true;

      // briefly flash the built-in LED to indicate a new value was received
      arduino.led.flash(RECEIVE_LED_FLASH_MS);
   }
};

WaveTelemetryHandler telemetryHandler(&status);
TelemetrySubscriber client(TELEMETRY_TOPIC, &status, &telemetryHandler);

void setup()
{
   SerialX::begin();
   arduino.begin();
   status.begin();
   status.setStatus(Status::STARTED);

   arduino.beginInit();
   displayFooter();

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &status);

   arduino.initClient("WebSocket", []() { client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &status);

   delay(1000); // provide time for the wave sensor to get a reading
}

float lastSensorReading = NAN;
unsigned long lastAcceptedMillis = 0;

void loop()
{
   client.loop();

   if (client.isStarted() == false)
   {
      return;
   }

   // get value measured from the bottom of the graph
   float sensorReading = client.getValue();
   float avgSensorReading = sensorReadings.get();

   if (newValueReceived && !isnan(sensorReading))
   {
      newValueReceived = false;

      if (receivedValues.length() > 0)
      {
         receivedValues += "\n";
      }

      unsigned long elapsedMillis = millis() - lastAcceptedMillis;
      float maxAllowedJump = MAX_RATE_CM_PER_SEC * (elapsedMillis / 1000.0f);

      if (!isnan(lastSensorReading) && fabs(sensorReading - lastSensorReading) > maxAllowedJump)
      {
         float rejectedDelta = avgSensorReading - sensorReading;
         std::ostringstream rejectedValueWithHeight;
         rejectedValueWithHeight << "*" << sensorReading << " (*" << std::fixed << std::setprecision(1) << rejectedDelta << ")";
         receivedValues += rejectedValueWithHeight.str();
         receivedValueCount++;
         rejectedSensorValueCount++;
      }
      else
      {
         lastSensorReading = sensorReading;
         lastAcceptedMillis = millis();

         sensorReadings.set(sensorReading);
         avgSensorReading = sensorReadings.get();

         float delta = avgSensorReading - sensorReading;
         waveHeight.set(delta);

         std::ostringstream valueWithHeight;
         valueWithHeight << sensorReading << " (" << std::fixed << std::setprecision(1) << delta << ")";
         receivedValues += valueWithHeight.str();
         receivedValueCount++;
      }
   }

   if (waveHeight.ready() == false)
   {
      return;
   }

   if (chartTimer.ready())
   {
      waterLevelChart.set(waveHeight.get() + WAVE_HEIGHT_MAX);

      // display values
      arduino.setCursor(0, 0);
      arduino.setTextSize(3);
      arduino.printlnR(waveHeight.get(), heightFormat, Color::VALUE);
      arduino.setCursor(0, ROLLING_RECT.y);

      displayRate.tick();
      waterLevelChart.draw(&arduino.display);
   }

   if (logTimer.ready())
   {
      Serial.println("------------------------------- Wave Data");
      Serial.println(String("Server Rate: ") + String(serverRate.get()) + " data pts per sec");
      Serial.println(String("Display Rate: ") + String(displayRate.get()) + " data pts per sec");
      Serial.println(String("Received Values (") + String(receivedValueCount) + ", " + String(rejectedSensorValueCount) + " rejected):");
      Serial.println(receivedValues.c_str());
      Serial.println();
      receivedValues.clear();
      receivedValueCount = 0;
      rejectedSensorValueCount = 0;
   }
}
