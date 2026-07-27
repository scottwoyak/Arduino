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

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_PLAYGROUND_SUPPORTED
#error "This sketch requires a Playground board (e.g. ESP32-S3 Dev Module wired as a Playground)."
#endif

#include "DisplayField.h"
#include "DisplayTableCellEditor.h"
#include "DisplayTableEditor.h"
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
Stopwatch sw(false);
RollingRate rate(RATE_NUM_SAMPLES);
Format topicFormat(20);
Format hostFormat(24);
Format sourceFormat(6);
Format statusFormat(24);
Format rateFormat("###/s");
Format lastValueFormat("+###.###");
std::string statusText = "Connecting to WiFi...";
std::string topicText = TELEMETRY_TOPIC;
std::string hostText = " ";
StringCell statusCell(&statusText, statusFormat);
StringCell topicCell(&topicText, topicFormat);
StringCell hostCell(&hostText, hostFormat);
long testFunctionIndex = 0;
long lastTestFunctionIndex = 0;
EnumCellEditor sourceCell(&testFunctionIndex,
   TEST_FUNCTION_LABELS, 0, sourceFormat);
IntCellEditor targetCell(&publishRatePerSec,
   MIN_PUBLISH_RATE_PER_SEC, MAX_PUBLISH_RATE_PER_SEC, PUBLISH_RATE_STEP, DEFAULT_PUBLISH_RATE_PER_SEC, rateFormat);
float rateValue = 0.0f;
ReadOnlyCell rateCell(&rateValue, rateFormat);
TableEditorRow statusCells[] =
{
   { "Status", &statusCell },
   { "Topic", &topicCell },
   { "Host", &hostCell },
   { "Source", &sourceCell },
   { "Target", &targetCell },
   { "Rate", &rateCell },
};
DisplayTableEditor table(&arduino, PREF_NAMESPACE, statusCells, 0, 0);
DisplayField* valueField = nullptr;
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
}

///
/// <summary>
/// Called when the WebSocket connection to the telemetry server is lost.
/// Restarts the device so it can reconnect from a clean state.
/// </summary>
///
void onDisconnected()
{
   Serial.println("Telemetry: WebSocket Disconnected");
   delay(1000);
   Util::reset();
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
}

///
/// <summary>
/// Called once the telemetry connection is fully started. Marks the table as connected.
/// </summary>
///
void onStarted()
{
   statusText = "Connected";

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
   int16_t valueWidth = arduino.charW() * lastValueFormat.length();
   int16_t valueX = (arduino.width() - valueWidth) / 2;
   int16_t valueY = valueAreaTop + (valueAreaHeight - arduino.charH()) / 2;
   valueField = new DisplayField(&arduino, Point16(valueX, valueY), lastValueFormat, 5);
   arduino.setTextSize(2);

   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
   while (WiFi.status() != WL_CONNECTED)
   {
   }

   statusText = "Connecting to Server...";
   table.draw();

   client.setCallbacks(onConnected, onDisconnected, nullptr, onText, onError, onStarted);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);

   Url url(client.getUrl().c_str());
   hostText = url.getHost().c_str();
   table.draw();
}

void loop()
{
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

   if (valueField != nullptr)
   {
      valueField->draw(lastValue, Color::LABEL, Color::VALUE);
   }
}
