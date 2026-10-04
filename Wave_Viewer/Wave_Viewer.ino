//
// Wave Viewer
//
// Subscribes to live wave-height telemetry over a WebSocket connection and renders a
// smoothed rolling bar chart on the display. The telemetry topic (Waves/Ultrasonic or
// Waves/Pressure, matching Wave_Publisher's sensor-selected topics) is prompted for at
// startup and remembered.
//
// The layout is computed at runtime from the display's dimensions, so the sketch runs on
// any display size, e.g. the Hosyond ESP32-S3 4" 480x320 display (Viewer board, the
// default) or the 240x135 display on the Feather ESP32-S3 TFT.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   receives live wave sensor readings as they arrive.
// - Applies short-term smoothing/buffering before updating the displayed value and chart.
// - On telemetry disconnect/error, keeps retrying in the background (throttled logging)
//   instead of resetting the device; the display simply stops updating until it reconnects.
// - Checks for a firmware update periodically.
//

// Uncomment this to build for the Feather ESP32-S3 TFT instead of the Hosyond ESP32-S3
// Viewer board.
//#define ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT

// Default: build for the Hosyond ESP32-S3 Viewer board's larger display. Also requires
// selecting the generic "ESP32S3 Dev Module" board in Visual Micro (that board has no
// dedicated board package entry).
#define ARDUINO_HOSYOND_ESP32_S3_VIEWER

// Declares which VLW font sizes this sketch actually uses (2 and 4 - see
// TEXT_SIZE/MIN_HEADER_TEXT_SIZE/MAX_HEADER_TEXT_SIZE below - plus 3, used for the
// status text and as this board's DEFAULT_HEADING_SIZE in printInitHeader() during
// boot), so ArduinoWithDisplay.h/Fonts/Roboto*.h only compile in the needed font data
// instead of all 7 sizes, reducing flash usage.
#define TEXT_SIZES_CUSTOM
#define TEXT_SIZE_2
#define TEXT_SIZE_3
#define TEXT_SIZE_4

#include <string>

constexpr const char* TELEMETRY_TOPICS[] = { "Waves/Ultrasonic", "Waves/Pressure" };
constexpr auto PREFERENCES_NAMESPACE = "WaveViewer";

// Selected at startup via prompt in setup().
std::string telemetryTopic;

#include <iomanip>
#include <sstream>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Hosyond ESP32-S3 Viewer or Feather ESP32-S3)."
#include "WrongBoard.h"
#endif

#include "BarChart.h"
#include "MovingBarChart.h"
#include "BufferedTimeSeries.h"
#include "LibraryVersion.h"
#include "RollingRate.h"
#include "RollingStats.h"
#include "SerialX.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "WiFiSettings.h"
#include "ViewerSketch.h"

// This sketch's own version (e.g. "1.0"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Wave_Viewer";

// ----------- Telemetry
SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .enableOTA = true,
};

TelemetryConfig TELEMETRY_CONFIG = {
   .prompts = TELEMETRY_TOPICS,
   .primary = TELEMETRY_RASPBERRY_ENDPOINT,
   .fallback = TELEMETRY_PRODUCTION_ENDPOINT,
   .deviceToken = TELEMETRY_DEVICE_TOKEN,
   .clientToken = TELEMETRY_CLIENT_TOKEN,
};

ViewerSketch sketch(SKETCH_CONFIG, TELEMETRY_CONFIG);
RollingRate displayRate(100);
RollingRate serverRate(100);
RollingStats sensorReadings(500);
std::string receivedValues;
uint32_t receivedValueCount = 0;
uint32_t rejectedSensorValueCount = 0;
bool newValueReceived = false;

// Elapsed time (reported by the telemetry server, via onSample) since the previous
// sample for this topic; more accurate than timing receipt locally via millis(), since
// it isn't affected by WiFi/processing jitter on this device.
int64_t lastSampleDtMicros = 0;

#ifdef ARDUINO_BUILTIN_LED_SUPPORTED
// ----------- Built-in LED (flashes on each received telemetry value)
constexpr uint16_t RECEIVE_LED_FLASH_MS = 20;
#endif

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

// A dropped telemetry connection keeps retrying in the background (see
// WebSocketsClient's built-in auto-reconnect) rather than resetting the device; without
// throttling, a persistent outage would otherwise flood the log with a message per
// retry attempt. RECONNECT_LOG_INTERVAL_S throttles that down to a single "still down"
// summary every 10 minutes.
constexpr float RECONNECT_LOG_INTERVAL_S = 600.0f;

BufferedTimeSeries waveHeight(BUFFER_TIME_SPAN_MS, BUFFER_RESOLUTION_MS);
Timer logTimer(LOG_INTERVAL_MS);
Timer chartTimer(CHART_UPDATE_MS);

Format heightFormat("###.# cm", Format::Alignment::RIGHT);

// ----------- Display layout
// The layout is computed at runtime from the display's dimensions (see initLayout) so
// that the sketch runs on displays of different sizes, e.g. the 480x320 Viewer board
// and the 240x135 Feather ESP32-S3 TFT.
constexpr uint8_t TEXT_SIZE = 2;
constexpr uint8_t MIN_HEADER_TEXT_SIZE = 2;
constexpr uint8_t MAX_HEADER_TEXT_SIZE = 4;
constexpr uint8_t HEADER_PADDING = 4;

// ----------- Rolling bar chart view
constexpr Color LakeBlue = Color565::fromRGB(0, 0, 255);

// Maximum expected magnitude of the wave height in either direction. The chart itself
// has no zero-baseline concept (bars always fill from their range minimum upward), so
// the chart is given a range of 0..2*WAVE_HEIGHT_MAX and values are shifted by
// +WAVE_HEIGHT_MAX before being plotted so that zero renders in the middle.
constexpr float WAVE_HEIGHT_MAX = 15;

// Set by initLayout() once the display dimensions are known.
uint8_t headerTextSize;
Rect16 rollingRect;
MovingBarChart* waterLevelChart = nullptr;

///
/// <summary>
/// Computes the display layout and constructs the rolling chart from the display's
/// actual dimensions, so the sketch renders correctly on displays of different sizes.
/// Must be called after arduino.begin(), once the display has been initialized.
/// </summary>
///
void initLayout()
{
   uint16_t displayWidth = sketch.arduino.width();
   uint16_t displayHeight = sketch.arduino.height();

   // use the largest header size where the topic still fits on a single line
   headerTextSize = MIN_HEADER_TEXT_SIZE;
   for (uint8_t size = MAX_HEADER_TEXT_SIZE; size > MIN_HEADER_TEXT_SIZE; size--)
   {
      if (sketch.arduino.charW(size) * telemetryTopic.length() <= displayWidth)
      {
         headerTextSize = size;
         break;
      }
   }

   uint16_t headerHeight = sketch.arduino.charH(headerTextSize) + HEADER_PADDING;

   rollingRect = { 0, headerHeight, displayWidth, (uint16_t)(displayHeight - headerHeight) };
   waterLevelChart = new MovingBarChart(rollingRect, RangeF(0, 2 * WAVE_HEIGHT_MAX), LakeBlue, Color::BLACK);
}

///
/// <summary>
/// Draws the sketch's title header (the telemetry topic) at the top of the display.
/// </summary>
///
void displayHeader()
{
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.setTextSize(headerTextSize);
   sketch.arduino.println(telemetryTopic, Color::HEADING);
}

///
/// <summary>
/// Handles telemetry lifecycle events for this sketch: clears the display once
/// started. Unlike the base class's default behavior, disconnects/failures/errors do
/// not reset the device; they're logged (throttled) and the underlying WebSocket keeps
/// retrying the connection in the background, while the main loop simply stops
/// updating (via isStarted()) until it reconnects.
/// </summary>
///
class WaveTelemetryHandler : public TelemetryEventHandler
{
private:
   ReconnectLogThrottle _reconnectLog{ telemetryTopic, RECONNECT_LOG_INTERVAL_S };

public:
   // set on every (re)start; the main loop clears and redraws the display, so the
   // "Telemetry... OK" init line printed during setup() is never wiped mid-init
   bool needsInitialDisplay = true;

   explicit WaveTelemetryHandler(IStatus* status) : TelemetryEventHandler(status, &sketch.arduino)
   {
   }

   void onStarted() override
   {
      TelemetryEventHandler::onStarted();
      _reconnectLog.reportSuccess();
      needsInitialDisplay = true;
   }

   void onDisconnected(const std::string& reason) override
   {
      _reconnectLog.reportFailure(std::string("Wave telemetry disconnected (") + reason + "); will keep retrying in the background");
   }

   void onConnectionFailed(const std::string& reason) override
   {
      _reconnectLog.reportFailure(std::string("Wave telemetry connection failed (") + reason + "); will keep retrying in the background");
   }

   void onError(const std::string& message) override
   {
      _reconnectLog.reportFailure(std::string("Wave telemetry error: ") + message + "; will keep retrying in the background");
   }

   void onReceiveText(const std::string& text) override
   {
      serverRate.tick();
      newValueReceived = true;

#ifdef ARDUINO_BUILTIN_LED_SUPPORTED
      // briefly flash the built-in LED to indicate a new value was received
      sketch.arduino.led.flash(RECEIVE_LED_FLASH_MS);
#endif
   }
};

WaveTelemetryHandler telemetryHandler(&sketch.arduino.status);

void setup()
{
   SerialX::begin();

   sketch.beginBanner();

   initLayout();

   telemetryTopic = sketch.resolveTopic(true);

   sketch.beginConnect();

   sketch.beginTelemetry(&telemetryHandler);
   sketch.getClient()->onSample([](const std::string& topic, double value, int64_t dtMicros)
   {
      lastSampleDtMicros = dtMicros;
   });

   delay(1000);

   Logger.logInitializationComplete();
}

float lastSensorReading = NAN;

void loop()
{
   sketch.loop();

   TelemetrySubscriber* client = sketch.getClient();

   if (client->isStarted() == false)
   {
      return;
   }

   if (telemetryHandler.needsInitialDisplay)
   {
      telemetryHandler.needsInitialDisplay = false;
      sketch.arduino.clearDisplay();
      displayHeader();
   }

   // get value measured from the bottom of the graph
   float sensorReading = client->getValue();
   float avgSensorReading = sensorReadings.get();

   if (newValueReceived && !isnan(sensorReading))
   {
      newValueReceived = false;

      if (receivedValues.length() > 0)
      {
         receivedValues += "\n";
      }

      float elapsedSecs = lastSampleDtMicros / 1000000.0f;
      float maxAllowedJump = MAX_RATE_CM_PER_SEC * elapsedSecs;

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

         sensorReadings.set(sensorReading);
         avgSensorReading = sensorReadings.get();

         float delta = avgSensorReading - sensorReading;
         waveHeight.set(delta, lastSampleDtMicros / 1000);

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
      waterLevelChart->set(waveHeight.get() + WAVE_HEIGHT_MAX);

      // display values
      sketch.arduino.setCursor(0, 0);
      sketch.arduino.setTextSize(3);
      sketch.arduino.printlnR(waveHeight.get(), heightFormat, Color::VALUE);
      sketch.arduino.setCursor(0, rollingRect.y);

      displayRate.tick();
      waterLevelChart->draw(&sketch.arduino.display);
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
