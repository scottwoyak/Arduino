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
constexpr const char* TEST_FUNCTION_LABELS[] = { "Const", "Random", "Normal", "Sin" };
constexpr size_t NUM_TEST_FUNCTIONS = sizeof(TEST_FUNCTION_LABELS) / sizeof(TEST_FUNCTION_LABELS[0]);
constexpr const char* PREF_NAMESPACE = "TelemetryPubPg";
ConstantTestSensor constantSensor;
RandomTestSensor randomSensor;
NormalTestSensor normalSensor;
SinTestSensor sinSensor;
ITestSensor* const TEST_FUNCTION_SENSORS[] = { &constantSensor, &randomSensor, &normalSensor, &sinSensor };
ITestSensor* sensor = nullptr;

// ----------- Publish Rate (adjustable live with Encoder A/B)
constexpr long DEFAULT_PUBLISH_RATE_PER_SEC = 10;
constexpr long MIN_PUBLISH_RATE_PER_SEC = 1;
constexpr long MAX_PUBLISH_RATE_PER_SEC = 200;
constexpr long PUBLISH_RATE_STEP = 1;
long publishRatePerSec = DEFAULT_PUBLISH_RATE_PER_SEC;
Timer publishTimer(1000UL / DEFAULT_PUBLISH_RATE_PER_SEC);

// ----------- Display Items
constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;
constexpr uint16_t RATE_NUM_SAMPLES = 100;
constexpr int16_t VALUE_PADDING_PX = 5;
constexpr float RECONNECT_COUNTDOWN_SECS = 5.0f;
Stopwatch sw(false);
TimerSecs reconnectTimer(RECONNECT_COUNTDOWN_SECS);
bool connected = false;
bool disconnected = false;
uint8_t lastCountdownSecs = 0;
RollingRate rate(RATE_NUM_SAMPLES);
Format topicFormat(20);
Format hostFormat(24);
Format sourceFormat(6);
Format rateFormat("###/s");
Format lastValueFormat("+###.###");
Format statusValueFormat(32, Format::Alignment::CENTER);
std::string statusText = "Connecting to WiFi...";
Color statusColor = Color::BLUE;
std::string topicText = TELEMETRY_TOPIC;
std::string hostText = " ";
StringValue topicValue(&topicText, topicFormat);
StringValue hostValue(&hostText, hostFormat);
long testFunctionIndex = 0;
long lastTestFunctionIndex = 0;
EnumEditor sourceEditor(&testFunctionIndex,
   TEST_FUNCTION_LABELS, 0, sourceFormat);
IntEditor targetEditor(&publishRatePerSec,
   MIN_PUBLISH_RATE_PER_SEC, MAX_PUBLISH_RATE_PER_SEC, PUBLISH_RATE_STEP, DEFAULT_PUBLISH_RATE_PER_SEC, rateFormat);
float rateValue = 0.0f;
FloatValue rateValueField(&rateValue, rateFormat);
FieldTableEditor::Row statusCells[] =
{
   { "Topic", &topicValue },
   { "Host", &hostValue },
   { "Source", &sourceEditor },
   { "Target", &targetEditor },
   { "Rate", &rateValueField },
};
FieldTableEditor table(&arduino, PREF_NAMESPACE, statusCells, 0, 0);
DisplayValue* valueField = nullptr;
DisplayValue* statusField = nullptr;
float lastValue = NAN;

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
   statusText = "Retrying...";
   statusColor = Color::RED;
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
}

void setup()
{
   SerialX::begin();
   arduino.begin();

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
   table.draw();

   arduino.setTextSize(5);
   int16_t valueAreaTop = table.getRect().bottom() + VALUE_PADDING_PX;
   int16_t valueAreaHeight = arduino.height() - valueAreaTop;
   int16_t valueAreaCenterX = arduino.width() / 2;
   int16_t valueAreaCenterY = valueAreaTop + valueAreaHeight / 2;
   valueField = new DisplayValue(&arduino, lastValueFormat, 5, DisplayValue::Alignment::CENTER);
   valueField->setPosition(valueAreaCenterX, valueAreaCenterY, VerticalAnchor::MIDDLE);

   arduino.setTextSize(2);
   statusField = new DisplayValue(&arduino, statusValueFormat, 2, DisplayValue::Alignment::CENTER);
   statusField->setPosition(valueAreaCenterX, valueAreaCenterY, VerticalAnchor::MIDDLE);

   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
   while (WiFi.status() != WL_CONNECTED)
   {
   }

   statusText = "Connecting to Server...";
   statusColor = Color::GREEN;
   statusField->draw(statusText, statusColor);

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
      if (secsLeft != lastCountdownSecs)
      {
         lastCountdownSecs = secsLeft;
         statusText = secsLeft > 0 ? "Retrying in " + std::to_string(secsLeft) + "s" : "Retrying...";
      }
   }

   if (arduino.buttonA.wasPressed())
   {
      Util::reset();
   }

   table.selectNext(arduino.encoderA.delta());

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
      float value = sensor->get();
      client.setValue(value);
      lastValue = value;
   }

   client.loop();

   if (sw.elapsedMillis() > RATE_UPDATE_INTERVAL_MS)
   {
      rateValue = rate.get();
      sw.reset();
   }

   table.draw();

   if (connected)
   {
      if (valueField != nullptr)
      {
         valueField->draw(lastValue, Color::VALUE);
      }
   }
   else if (statusField != nullptr)
   {
      statusField->draw(statusText, statusColor);
   }
}
