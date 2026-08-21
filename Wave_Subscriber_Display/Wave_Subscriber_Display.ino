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

// ----------- Built-in LED (flashes on each received telemetry value)
constexpr uint16_t RECEIVE_LED_FLASH_MS = 20;

// ----------- Buffering / smoothing
constexpr unsigned long LOG_INTERVAL_MS = 5000;
constexpr unsigned long BUFFER_TIME_SPAN_MS = 2000;
constexpr unsigned long BUFFER_RESOLUTION_MS = 100;
constexpr unsigned long CHART_UPDATE_MS = 10; // 100 fps

// Maximum plausible change (cm) between consecutive raw sensor readings; larger jumps are
// glitches/dropouts and are rejected before updating the rolling baseline average.
constexpr float MAX_SENSOR_JUMP = 20;

// Maximum plausible change (cm) in the computed wave-height delta between consecutive
// buffered samples; larger jumps are rejected so a single bad delta doesn't spike the chart.
constexpr float MAX_DELTA_JUMP = 5;

BufferedTimeSeries waveHeight(BUFFER_TIME_SPAN_MS, BUFFER_RESOLUTION_MS);
Timer bufferTimer(BUFFER_RESOLUTION_MS);
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
constexpr RangeF ROLLING_RANGE = { 0, 40 };

constexpr Rect16 ROLLING_RECT(0, HEADER_HEIGHT + SUBHEADING_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT - SUBHEADING_HEIGHT);
MovingBarChart rollingChart(ROLLING_RECT, ROLLING_RANGE, LakeBlue, Color::BLACK);

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

      if (receivedValues.length() > 0)
      {
         receivedValues += ",";
      }
      receivedValues += text;

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

float lastDelta = 0;
float lastSensorReading = NAN;

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

   if (bufferTimer.ready())
   {
      if (isnan(sensorReading))
      {
         return;
      }

      if (!isnan(lastSensorReading) && fabs(sensorReading - lastSensorReading) > MAX_SENSOR_JUMP)
      {
         return;
      }
      lastSensorReading = sensorReading;

      sensorReadings.set(sensorReading);
      avgSensorReading = sensorReadings.get();

      float delta = avgSensorReading - sensorReading;
      if (fabs(delta - lastDelta) < MAX_DELTA_JUMP)
      {
         waveHeight.set(delta);
         lastDelta = delta;
      }
   }

   if (waveHeight.ready() == false)
   {
      return;
   }

   if (chartTimer.ready())
   {
      rollingChart.set((ROLLING_RANGE.min + ROLLING_RANGE.max) / 2.0 + waveHeight.get());

      // display values
      arduino.setCursor(0, 0);
      arduino.setTextSize(3);
      arduino.printlnR(waveHeight.get(), heightFormat, Color::VALUE);
      arduino.setCursor(0, ROLLING_RECT.y);

      displayRate.tick();
      rollingChart.draw(&arduino.display);
   }

   if (logTimer.ready())
   {
      Serial.println("------------------------------- Wave Data");
      Serial.println(String("Server Rate: ") + String(serverRate.get()) + " data pts per sec");
      Serial.println(String("Display Rate: ") + String(displayRate.get()) + " data pts per sec");
      Serial.println(String("Sensor Reading: ") + String(sensorReading));
      Serial.println(String("Average Reading: ") + String(avgSensorReading));
      Serial.println(String("Wave Height: ") + String(waveHeight.get()));
      Serial.println(String("Received Values: ") + receivedValues.c_str());
      Serial.println();
      receivedValues.clear();
   }
}
