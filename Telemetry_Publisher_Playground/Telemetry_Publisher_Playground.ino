//
// Telemetry data publisher with interactive Playground controls.
//
// Publishes mock sensor test data to a telemetry server via WebSocket connection,
// using the shared Publisher lifecycle (banner, force-prompt site window, WiFi,
// rebooter, OTA, and Influx setup/log mirroring only). This sketch has no enclosure
// and does not upload any sensor/enclosure values to InfluxDB.
// Displays connection status, topic, host, source, publish rate, and message rate on a TFT
// display, same as Telemetry_Publisher_Display, but runs on a Playground board so the source
// (mock test function) and publish rate can be selected/adjusted live: Encoder A cycles the
// selected field and Encoder B adjusts its value.
//

#include <Arduino.h>
#include <cmath>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#include "WrongBoard.h"
#endif
#ifndef ARDUINO_PLAYGROUND_SUPPORTED
#error "This sketch requires a Playground board (e.g. ESP32-S3 Dev Module wired as a Playground)."
#include "WrongBoard.h"
#endif

#include "DisplayValue.h"
#include "FieldTableEditor.h"
#include "LibraryVersion.h"
#include "ScatterPlot.h"
#include "TestSensor.h"
#include "ValueEditor.h"
#include "WiFiSettings.h"

#include "PublisherSketch.h"

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Telemetry_Publisher_Playground";

// ----------- Telemetry topic
// Fixed topic; this is a testing sketch with no enclosure, no InfluxDB upload, and no
// selectable site configuration.
constexpr auto TELEMETRY_TOPIC = "Test";

// ----------- Test Function Selection
constexpr const char* TEST_FUNCTION_LABELS[] = { "Const", "Random", "Normal", "Sin", "Wave" };
constexpr const char* PREF_NAMESPACE = "TelemetryPubPg";
ConstantTestSensor constantSensor;
RandomTestSensor randomSensor;
NormalTestSensor normalSensor;
SinTestSensor sinSensor;
WaveTestSensor waveSensor;
ITestSensor* const TEST_FUNCTION_SENSORS[] = { &constantSensor, &randomSensor, &normalSensor, &sinSensor, &waveSensor };
ITestSensor* sensor = nullptr;

// ----------- Publish Rate (adjustable live with Encoder A/B)
constexpr long DEFAULT_PUBLISH_RATE_PER_SEC = 10;
constexpr long MIN_PUBLISH_RATE_PER_SEC = 1;
constexpr long MAX_PUBLISH_RATE_PER_SEC = 500;
TimerMicros publishTimer(1000000UL / DEFAULT_PUBLISH_RATE_PER_SEC);

// ----------- Reconnect/Retry Tracking
constexpr float RECONNECT_COUNTDOWN_SECS = 10.0f;
TimerSecs reconnectTimer(RECONNECT_COUNTDOWN_SECS);
bool disconnected = false;
uint8_t lastCountdownSecs = 0;
uint16_t retryCount = 0;
uint16_t lastRetryCount = 0;

// ----------- Message Rate
constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;
Timer rateDisplayTimer(RATE_UPDATE_INTERVAL_MS);
FloatValue rateValueField("###/s");

// ----------- Connection Status
bool connected = false;
std::string statusText = "Connecting to WiFi...";
Color statusColor = Color::BLUE;

// ----------- Server Info (host/topic, selectable live via Encoder A/B)
StringValue topicValue("##################", TELEMETRY_TOPIC);
StringValue hostValue("##################", " ");
EnumEditor sourceEditor(TEST_FUNCTION_LABELS, 0, "######");
constexpr const char* DISPLAY_LABELS[] = { "Off", "On" };
EnumEditor displayEditor(DISPLAY_LABELS, 1, "###");
ScaledStepIntEditor targetEditor(
   MIN_PUBLISH_RATE_PER_SEC, MAX_PUBLISH_RATE_PER_SEC, DEFAULT_PUBLISH_RATE_PER_SEC, "###/s");

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = TELEMETRY_TOPIC,
   .decimals = 3,
   .maxPublishRatePerSec = MAX_PUBLISH_RATE_PER_SEC,
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREF_NAMESPACE,
   .enableOTA = true,
};

// No Influx site table, so Influx isn't used.
PublisherSketch sketch(PUBLISHER_CONFIG, {}, TELEMETRY_CONFIG);

// ----------- Status Table
FieldTableEditor::Row tableCells[] =
{
   { "Server" },
   { "Host", &hostValue },
   { "Topic", &topicValue },
   { "Published Content" },
   { "Source", &sourceEditor },
   { "Sampling", &targetEditor },
   { "Published", &rateValueField },
   { "Plot/Value", &displayEditor },
};
FieldTableEditor table(&sketch.arduino, PREF_NAMESPACE, tableCells);

// ----------- Value/Status Display
DisplayValue value(&sketch.arduino, Format("###.###"), 5, DisplayValue::Alignment::DECIMAL);
DisplayValue status(&sketch.arduino, Format(32, Format::Alignment::LEFT), 2, DisplayValue::Alignment::LEFT);
float lastValue = NAN;
std::string lastErrorMsg = "";
std::string lastDrawnErrorMsg = "";
bool plotVisible = false;

// Set once setup() has positioned the table/value/status/error areas. Telemetry events can
// fire during sketch.begin(), before that, when these areas still sit at (0, 0) and clearing
// them would erase the init screen.
bool layoutReady = false;

// ----------- Error Message Area (plain print, manually cleared, so long messages can wrap)
int16_t errorAreaX = 0;
int16_t errorAreaY = 0;
constexpr int16_t ERROR_AREA_HEIGHT_PX = 40;

// ----------- Published Value Scatter Plot (bottom of display, 5 second rolling span)
constexpr unsigned long PLOT_SPAN_MS = 5000UL;
ScatterPlot valuePlot(&sketch.arduino, Rect16{}, "##.#s", "###.###");
TimedScatterPlotSeries* valueSeries = valuePlot.createTimedSeries(PLOT_SPAN_MS);
constexpr uint8_t VALUE_SERIES_POINT_SIZE = 2;
constexpr uint8_t VALUE_SERIES_MAX_POINT_SIZE = 3;

///
/// <summary>
/// Clears the error message area, which uses plain wrapped text rather than a
/// DisplayValue sprite, so it must be erased manually before drawing new text or
/// when no error is currently active.
/// </summary>
///
void clearErrorArea()
{
   if (!layoutReady)
   {
      return;
   }

   sketch.arduino.fillRect(errorAreaX, errorAreaY, sketch.arduino.width() - errorAreaX, ERROR_AREA_HEIGHT_PX, Color::BLACK);
   lastDrawnErrorMsg.clear();
}

///
/// <summary>
/// Handles telemetry lifecycle events for this sketch: updates on-screen status
/// text/colors, tracks the reconnect countdown, ticks the message rate, and shows
/// errors before resetting.
/// </summary>
///
class PlaygroundTelemetryHandler : public TelemetryEventHandler
{
private:
   ///
   /// <summary>
   /// Shared handling for onDisconnected() and onConnectionFailed(): starts the
   /// reconnect countdown (only on the first transition into the disconnected state,
   /// since the underlying WebSocketsClient reports these events repeatedly - roughly
   /// every reconnect attempt - while the connection remains down) and clears the
   /// stale displayed value.
   /// </summary>
   ///
   void _onDisconnectedOrConnectionFailed()
   {
      if (!disconnected)
      {
         disconnected = true;
         lastCountdownSecs = 0;
         reconnectTimer.reset();
         statusColor = Color::RED;
      }

      retryCount++;

      if (connected && layoutReady)
      {
         // switching from drawing value to drawing status; erase the stale number
         value.clear();
      }
      connected = false;
   }

public:
   explicit PlaygroundTelemetryHandler(IStatus* status, ArduinoWithDisplay* display) : TelemetryEventHandler(status, display)
   {
   }

   void onConnected() override
   {
      statusText = "Publishing Topic...";
      statusColor = Color::LIME;
      disconnected = false;
      connected = false;
      retryCount = 0;
      lastErrorMsg.clear();
      clearErrorArea();
   }

   ///
   /// <remarks>
   /// The underlying WebSocketsClient reports this event repeatedly (roughly every
   /// reconnect attempt) while the connection remains down, not just once at the initial
   /// disconnect. The countdown timer is therefore only (re)started the first time we
   /// transition into the disconnected state, so repeated disconnect events don't keep
   /// resetting the visible countdown back to its starting value.
   /// </remarks>
   ///
   void onDisconnected(const std::string& reason) override
   {
      Serial.println("Telemetry: WebSocket Disconnected: " + String(reason.c_str()));
      _onDisconnectedOrConnectionFailed();
      TelemetryEventHandler::onDisconnected(reason);
   }

   ///
   /// <remarks>
   /// The WebSocket never successfully connected before the socket was torn down (e.g.
   /// the server isn't running/reachable). Handled the same way as onDisconnected(),
   /// rather than the default onConnectionFailed() behavior of resetting the device.
   /// </remarks>
   ///
   void onConnectionFailed(const std::string& reason) override
   {
      Serial.println("Telemetry: WebSocket Connection Failed: " + String(reason.c_str()));
      _onDisconnectedOrConnectionFailed();
      TelemetryEventHandler::onConnectionFailed(reason);
   }

   void onError(const std::string& message) override
   {
      Serial.println("Telemetry Error: " + String(message.c_str()));
      lastErrorMsg = message;
      statusColor = Color::RED;

      if (connected && layoutReady)
      {
         // switching from drawing value to drawing status; erase the stale number
         value.clear();
      }
      connected = false;

      TelemetryEventHandler::onError(message);
   }

   void onStarted() override
   {
      statusText = "Connected";
      statusColor = Color::LIME;
      connected = true;
      retryCount = 0;
      lastErrorMsg.clear();

      // status's sprite is wider than value's, so switching from drawing status to
      // drawing value would otherwise leave stale status text visible around value
      if (layoutReady)
      {
         status.clear();
      }
      clearErrorArea();

      rateDisplayTimer.reset();

      TelemetryEventHandler::onStarted();
   }
};

PlaygroundTelemetryHandler telemetryHandler(sketch.getStatus(), &sketch.arduino);

///
/// <summary>
/// Selects the sensor for the current sourceEditor selection and begins it.
/// </summary>
///
void selectTestFunction()
{
   sensor = TEST_FUNCTION_SENSORS[sourceEditor.get()];
   sensor->begin();
   valuePlot.setYAxisFormat(sensor->getFormatStr().c_str());
   valuePlot.clear();
}

void setup()
{
   sketch.setTelemetryHandler(&telemetryHandler);
   sketch.begin();

   valueSeries->pointSize = VALUE_SERIES_POINT_SIZE;

   hostValue.set(DeviceServerClient::getHost().c_str());
   sketch.completeInitialization();

   sketch.arduino.setTextSize(3);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Publisher Playground", Color::HEADING);
   sketch.arduino.moveCursorY(sketch.arduino.charH() / 4);

   sketch.arduino.setTextSize(2);
   Point16 tablePos = sketch.arduino.getCursor();
   table.setPosition(tablePos);
   table.load();
   selectTestFunction();
   publishTimer.setDurationMs(1000000UL / targetEditor.get());

   int16_t valueAreaCenterX = sketch.arduino.width() * 3 / 4;
   int16_t valueAreaCenterY = table.getRect().top() + table.getRect().height / 2;

   sketch.arduino.setTextSize(5);
   value.setPosition(valueAreaCenterX, valueAreaCenterY, VerticalAnchor::MIDDLE);

   sketch.arduino.setTextSize(2);
   constexpr int16_t MESSAGE_PADDING_PX = 5;
   constexpr int16_t ROW_GAP_PX = 4;
   int16_t messageAreaX = 0;
   int16_t messageTop = table.getRect().bottom() + MESSAGE_PADDING_PX + status.height() + ROW_GAP_PX;
   status.setPosition(messageAreaX, messageTop, VerticalAnchor::TOP);

   errorAreaX = messageAreaX;
   errorAreaY = messageTop + status.height() + ROW_GAP_PX;
   sketch.arduino.display.setTextWrap(true);

   int16_t plotTop = table.getRect().bottom() + MESSAGE_PADDING_PX;
   valuePlot.setRect(0, plotTop, sketch.arduino.width(), sketch.arduino.height() - plotTop);
   valuePlot.setColors(Color::BLACK, Color::BLACK, Color::GRAY, Color::GRAY);
   valuePlot.setShowXMinMaxValue(false);
   valuePlot.setShowXRangeValue(true);
   valuePlot.setShowYRangeValue(false);
   valuePlot.setYAxisMode(ScatterPlot::AxisMode::GROW_ONLY);
   valueSeries->showPoints = true;
   valueSeries->showLines = false;

   layoutReady = true;
}

void loop()
{
   if (disconnected)
   {
      uint8_t secsLeft = static_cast<uint8_t>(ceil(reconnectTimer.remaining()));
      if (secsLeft != lastCountdownSecs || retryCount != lastRetryCount)
      {
         lastCountdownSecs = secsLeft;
         lastRetryCount = retryCount;
         statusText = secsLeft > 0 ? "Retrying in " + std::to_string(secsLeft) + "s" : "Retrying...";
         statusColor = Color::RED;
      }
   }

   if (sketch.arduino.buttonA.wasPressed())
   {
      Util::reset(0.0f, "Manual reset (button A)");
   }

   table.selectNext(sketch.arduino.encoderA.delta());

   if (sketch.arduino.encoderB.button.wasPressed())
   {
      if (valueSeries->showLines)
      {
         valueSeries->showLines = false;
         valueSeries->showPoints = true;
         valueSeries->pointSize = 1;
      }
      else if (valueSeries->pointSize < VALUE_SERIES_MAX_POINT_SIZE)
      {
         valueSeries->pointSize++;
      }
      else
      {
         valueSeries->showPoints = false;
         valueSeries->showLines = true;
      }
   }

   int32_t adjustDelta = sketch.arduino.encoderB.delta();
   if (adjustDelta != 0)
   {
      table.adjustSelected(adjustDelta);
      table.save();

      if (sourceEditor.hasChanged())
      {
         selectTestFunction();
      }

      publishTimer.setDurationMs(1000000UL / targetEditor.get());

      if (displayEditor.hasChanged())
      {
         value.clear();
         valuePlot.clear();
         plotVisible = false;
      }
   }

   const bool showPlotAndValue = displayEditor.get() == 1;

   if (publishTimer.ready())
   {
      float sensorValue = sensor->get();
      sketch.client()->setValue(sensorValue);
      lastValue = sensorValue;

      if (showPlotAndValue)
      {
         valueSeries->add(sensorValue);
      }
   }

   sketch.loop();

   if (rateDisplayTimer.ready())
   {
      rateValueField.set(sketch.client()->getRate());
   }

   table.draw();

   // Only show
   // error message
   if (showPlotAndValue && connected && lastErrorMsg.empty())
   {
      if (!plotVisible)
      {
         valuePlot.clear();
         plotVisible = true;
      }

      valueSeries->updateWindow(millis());
      valuePlot.draw();
   }
   else if (plotVisible)
   {
      valuePlot.clear();
      plotVisible = false;
   }

   if (connected)
   {
      if (showPlotAndValue && !isnan(lastValue))
      {
         value.draw(lastValue, Color::VALUE);
      }
   }
   else
   {
      status.draw(statusText, statusColor);

      if (!lastErrorMsg.empty() && lastErrorMsg != lastDrawnErrorMsg)
      {
         clearErrorArea();
         sketch.arduino.setCursor(errorAreaX, errorAreaY);
         sketch.arduino.println(lastErrorMsg.c_str(), Color::RED);
         lastDrawnErrorMsg = lastErrorMsg;
      }
   }
}
