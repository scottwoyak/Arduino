//
// Telemetry data publisher with interactive Playground controls.
//
// Publishes mock sensor test data to a telemetry server via WebSocket connection.
// Displays connection status, topic, host, source, publish rate, and message rate on a TFT
// display, same as Telemetry_Publisher_Display, but runs on a Playground board so the source
// (mock test function) and publish rate can be selected/adjusted live: Encoder A cycles the
// selected field and Encoder B adjusts its value.
//
// Uncomment TELEMETRY_LOCAL to use a local telemetry server instead of the remote.
// Hardware: ESP32-S3 Dev Module wired as a Playground board (TFT display + rotary encoders).
//

// Uncomment to use local telemetry server instead of remote
#define TELEMETRY_LOCAL

#include <Arduino.h>
#include <WiFi.h>
#include <cmath>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_PLAYGROUND_SUPPORTED
#error "This sketch requires a Playground board (e.g. ESP32-S3 Dev Module wired as a Playground)."
#endif

#include "ValueEditor.h"
#include "FieldTableEditor.h"
#include "DisplayValue.h"
#include "RollingRate.h"
#include "ScatterPlot.h"
#include "SerialX.h"
#include "Stopwatch.h"
#include "TelemetryClient.h"
#include "TestSensor.h"
#include "Timer.h"
#include "Url.h"

#include "WiFiSettings.h"

// ----------- Telemetry
constexpr const char* TELEMETRY_TOPIC = "Test";
constexpr uint8_t TELEMETRY_DECIMAL_PLACES = 3;
TelemetryPublisher client(TELEMETRY_TOPIC, TELEMETRY_DECIMAL_PLACES);

// ----------- The Board
Arduino arduino;

// ----------- Test Function Selection (source, selectable live via Encoder A/B)
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
constexpr long MAX_PUBLISH_RATE_PER_SEC = 200;
constexpr long PUBLISH_RATE_STEP = 1;
long publishRatePerSec = DEFAULT_PUBLISH_RATE_PER_SEC;
Timer publishTimer(1000UL / DEFAULT_PUBLISH_RATE_PER_SEC);

// ----------- Reconnect/Retry Tracking
constexpr float RECONNECT_COUNTDOWN_SECS = 10.0f;
TimerSecs reconnectTimer(RECONNECT_COUNTDOWN_SECS);
bool disconnected = false;
uint8_t lastCountdownSecs = 0;
uint16_t retryCount = 0;
uint16_t lastRetryCount = 0;

// ----------- Message Rate
constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;
constexpr uint16_t RATE_NUM_SAMPLES = 10;
Stopwatch sw(false);
RollingRate rate(RATE_NUM_SAMPLES);
float rateValue = 0.0f;
FloatValue rateValueField(&rateValue, "###/s");

// ----------- Connection Status
bool connected = false;
std::string statusText = "Connecting to WiFi...";
Color statusColor = Color::BLUE;

// ----------- Server Info (host/topic, selectable live via Encoder A/B)
std::string topicText = TELEMETRY_TOPIC;
std::string hostText = " ";
StringValue topicValue(&topicText, "##################");
StringValue hostValue(&hostText, "##################");
long testFunctionIndex = 0;
long lastTestFunctionIndex = 0;
EnumEditor sourceEditor(&testFunctionIndex, TEST_FUNCTION_LABELS, 0, "######");
IntEditor targetEditor(&publishRatePerSec,
   MIN_PUBLISH_RATE_PER_SEC, MAX_PUBLISH_RATE_PER_SEC, PUBLISH_RATE_STEP, DEFAULT_PUBLISH_RATE_PER_SEC, "###/s");

// ----------- Status Table
FieldTableEditor::Row tableCells[] =
{
   { "Server" },
   { "Host", &hostValue },
   { "Topic", &topicValue },
   { "Published Content" },
   { "Source", &sourceEditor },
   { "Sampling Rate", &targetEditor },
   { "Published Rate", &rateValueField },
};
FieldTableEditor table(&arduino, PREF_NAMESPACE, tableCells);

// ----------- Value/Status Display
DisplayValue value(&arduino, Format("###.###"), 5, DisplayValue::Alignment::DECIMAL);
DisplayValue status(&arduino, Format(32, Format::Alignment::LEFT), 2, DisplayValue::Alignment::LEFT);
float lastValue = NAN;
std::string lastErrorMsg = "";
std::string lastDrawnErrorMsg = "";
bool plotVisible = false;

// ----------- Error Message Area (plain print, manually cleared, so long messages can wrap)
int16_t errorAreaX = 0;
int16_t errorAreaY = 0;
constexpr int16_t ERROR_AREA_HEIGHT_PX = 40;

// ----------- Published Value Scatter Plot (bottom of display, 5 second rolling span)
constexpr unsigned long PLOT_SPAN_MS = 5000UL;
ScatterPlot valuePlot(&arduino, Rect16{}, "##.#s", "###.###");
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
   arduino.fillRect(errorAreaX, errorAreaY, arduino.width() - errorAreaX, ERROR_AREA_HEIGHT_PX, Color::BLACK);
   lastDrawnErrorMsg.clear();
}

///
/// <summary>
/// Called when the WebSocket connection to the telemetry server is established.
/// </summary>
///
void onConnected()
{
   Serial.println("Telemetry: WebSocket Connected");
   statusText = "Publishing Topic...";
   statusColor = Color::GREEN;
   disconnected = false;
   connected = false;
   retryCount = 0;
   lastErrorMsg.clear();
   clearErrorArea();
}

///
/// <summary>
/// Called when the WebSocket connection to the telemetry server is lost. Shows a
/// reconnect countdown with the disconnect reason in the status row rather than
/// resetting the device; the WebSocket client retries the connection automatically.
/// </summary>
/// <remarks>
/// The underlying WebSocketsClient reports this event repeatedly (roughly every
/// reconnect attempt) while the connection remains down, not just once at the initial
/// disconnect. The countdown timer is therefore only (re)started the first time we
/// transition into the disconnected state, so repeated disconnect events don't keep
/// resetting the visible countdown back to its starting value.
/// </remarks>
/// <param name="reason">The disconnect reason reported by TelemetryClient.</param>
///
void onDisconnected(std::string reason)
{
   Serial.println("Telemetry: WebSocket Disconnected: " + String(reason.c_str()));

   if (!disconnected)
   {
      disconnected = true;
      lastCountdownSecs = 0;
      reconnectTimer.reset();
      statusColor = Color::RED;
   }

   retryCount++;

   if (connected)
   {
      // switching from drawing value to drawing status; erase the stale number
      value.clear();
   }
   connected = false;
}

///
/// <summary>
/// Called when a text message is received from the telemetry server.
/// </summary>
/// <param name="payload">Message payload (unused)</param>
///
void onText(std::string payload)
{
   rate.tick();
}

///
/// <summary>
/// Called when a telemetry client error occurs (e.g. the server rejected the publish
/// request). The client automatically retries the request after a delay, so this just
/// updates the status row rather than resetting the device.
/// </summary>
/// <param name="msg">Error message reported by the server</param>
///
void onError(std::string msg)
{
   Serial.println("Telemetry Error: " + String(msg.c_str()));
   retryCount++;
   lastErrorMsg = msg;
   lastCountdownSecs = 0;
   statusColor = Color::RED;

   if (connected)
   {
      // switching from drawing value to drawing status; erase the stale number
      value.clear();
   }
   connected = false;
}

///
/// <summary>
/// Called once the telemetry connection is fully started. Marks the table as connected.
/// </summary>
///
void onStarted()
{
   statusText = "Connected";
   statusColor = Color::GREEN;
   connected = true;
   retryCount = 0;
   lastErrorMsg.clear();

   // status's sprite is wider than value's, so switching from drawing status to
   // drawing value would otherwise leave stale status text visible around value
   status.clear();
   clearErrorArea();

   rate.reset();
   sw.start();
}

///
/// <summary>
/// Selects the sensor for the current testFunctionIndex and begins it.
/// </summary>
///
void selectTestFunction()
{
   sensor = TEST_FUNCTION_SENSORS[testFunctionIndex];
   sensor->begin();
   valuePlot.setYAxisFormat(sensor->getFormatStr().c_str());
   valuePlot.clear();
}

void setup()
{
   SerialX::begin();
   arduino.begin();

   valueSeries->pointSize = VALUE_SERIES_POINT_SIZE;

   arduino.setTextSize(3);
   arduino.setCursor(0, 0);
   arduino.println("Publisher", Color::HEADING);
   arduino.moveCursorY(4);

   arduino.setTextSize(2);
   Point16 tablePos = arduino.getCursor();
   table.setPosition(tablePos);
   table.load();
   selectTestFunction();
   lastTestFunctionIndex = testFunctionIndex;
   publishTimer.setDurationMs(1000UL / publishRatePerSec);
   table.draw();

   int16_t valueAreaCenterX = arduino.width() * 3 / 4;
   int16_t valueAreaCenterY = table.getRect().top() + table.getRect().height / 2;

   arduino.setTextSize(5);
   value.setPosition(valueAreaCenterX, valueAreaCenterY, VerticalAnchor::MIDDLE);

   arduino.setTextSize(2);
   constexpr int16_t MESSAGE_PADDING_PX = 5;
   int16_t messageAreaX = 0;
   int16_t messageTop = table.getRect().bottom() + MESSAGE_PADDING_PX + status.height() + 4;
   status.setPosition(messageAreaX, messageTop, VerticalAnchor::TOP);

   errorAreaX = messageAreaX;
   errorAreaY = messageTop + status.height() + 4;
   arduino.display.setTextWrap(true);

   int16_t plotTop = table.getRect().bottom() + MESSAGE_PADDING_PX;
   valuePlot.setRect(0, plotTop, arduino.width(), arduino.height() - plotTop);
   valuePlot.setShowXMinMaxValue(false);
   valuePlot.setShowXRangeValue(true);
   valuePlot.setShowYRangeValue(false);
   valuePlot.setYAxisMode(ScatterPlot::AxisMode::GROW_ONLY);
   valueSeries->showPoints = true;
   valueSeries->showLines = false;

   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
   while (WiFi.status() != WL_CONNECTED)
   {
   }

   statusText = "Connecting to Server...";
   statusColor = Color::GREEN;
   status.draw(statusText, statusColor);

   client.setCallbacks(onConnected, onDisconnected, nullptr, onText, onError, onStarted);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);

   Url url(client.getUrl().c_str());
   hostText = url.getHost().c_str();
   table.draw();
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
   else if (client.isStartRetryPending())
   {
      uint8_t secsLeft = static_cast<uint8_t>(ceil(client.getStartRetryRemainingSecs()));
      if (secsLeft != lastCountdownSecs || retryCount != lastRetryCount)
      {
         lastCountdownSecs = secsLeft;
         lastRetryCount = retryCount;
         statusText = secsLeft > 0 ? "Retrying in " + std::to_string(secsLeft) + "s" : "Retrying...";
         statusColor = Color::RED;
      }
   }

   if (arduino.buttonA.wasPressed())
   {
      Util::reset();
   }

   table.selectNext(arduino.encoderA.delta());

   if (arduino.encoderB.button.wasPressed())
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

   int32_t adjustDelta = arduino.encoderB.delta();
   if (adjustDelta != 0)
   {
      table.adjustSelected(adjustDelta);
      table.save();

      if (testFunctionIndex != lastTestFunctionIndex)
      {
         selectTestFunction();
         lastTestFunctionIndex = testFunctionIndex;
      }

      publishTimer.setDurationMs(1000UL / publishRatePerSec);
   }

   if (publishTimer.ready())
   {
      float sensorValue = sensor->get();
      client.setValue(sensorValue);
      lastValue = sensorValue;
      valueSeries->add(sensorValue);
   }

   client.loop();

   if (sw.elapsedMillis() > RATE_UPDATE_INTERVAL_MS)
   {
      rateValue = rate.get();
      sw.reset();
   }

   table.draw();

   // Only show the plot once the error message area is clear; otherwise a long wrapped
   // error message could overlap the plot region above it.
   if (connected && lastErrorMsg.empty())
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
      value.draw(lastValue, Color::VALUE);
   }
   else
   {
      status.draw(statusText, statusColor);

      if (!lastErrorMsg.empty() && lastErrorMsg != lastDrawnErrorMsg)
      {
         clearErrorArea();
         arduino.setCursor(errorAreaX, errorAreaY);
         arduino.println(lastErrorMsg.c_str(), Color::RED);
         lastDrawnErrorMsg = lastErrorMsg;
      }
   }
}
