
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>

#include <WebSocketsClient.h>
#include "Feather.h"
#include "SerialX.h"
#include "WiFiSettings.h"
#include "Stopwatch.h"
#include "RateTracker.h"
#include "TelemetryClient.h"

Feather feather;

const char* webSocketServerHost = "192.168.1.43"; // e.g., "192.168.1.100" or "example.com"
constexpr uint16_t webSocketServerPort = 5029;    // Port the server is listening on
const char* webSocketPath = "/";                  // WebSocket path, typically "/"

WiFiMulti WiFiMulti;
Stopwatch sw(false);
RollingRateTracker rate;
Point16 ratePos;

Format rateFormat("###/s");

// forward declarations
TelemetryPublisher client("ArduinoTest", 3);


float getValue()
{
   constexpr ulong period = 2 * 1000 * 1000;
   float x = 2 * std::numbers::pi * (((float)(micros() % period)) / period);
   return sin(x);
}

void setup()
{
   SerialX::begin();
   feather.begin();

   // Connect to WiFi
   WiFiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

   feather.setTextSize(2);
   feather.println("Initializing", Color::HEADING2);
   feather.moveCursorY(4);

   feather.print("WiFi...", Color::LABEL);
   while (WiFiMulti.run() != WL_CONNECTED)
   {
      feather.print(".", Color::LABEL);
   }
   feather.printlnR("OK", Color::VALUE);
   feather.moveCursorY(1);


   feather.print("WebSocket...", Color::LABEL);

   client.setCallbacks(onConnected, onDisconnected, onText, onError, onStarted);
   client.begin(webSocketServerHost, webSocketServerPort, webSocketPath);
}

void onConnected()
{
   Serial.println("Connected");
}

void onDisconnected()
{
   Serial.println("Disconnected");
   delay(1000); // time for Serial to print
   Util::reset();
}

void onText(std::string payload)
{
   rate.tick();
}

void onError(std::string msg)
{
   feather.setTextSize(2);
   feather.clearDisplay();
   feather.display.setTextWrap(true);
   feather.println(msg, Color::RED);
   Util::reset(10);
}

void onStarted()
{
   // print the last ok for the WebSocket
   feather.printlnR("OK", Color::VALUE);
   delay(1000); // so the user can see the message

   // display the GUI
   feather.clearDisplay();
   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Publisher", Color::HEADING);

   feather.setTextSize(2);
   feather.moveCursorY(feather.charH() / 2);

   feather.print(" Topic: ", Color::LABEL);
   feather.println(client.getTopic(), Color::VALUE);
   feather.moveCursorY(1);

   feather.print("  Rate: ", Color::LABEL);
   ratePos = feather.getCursor();
   feather.println("---", Color::VALUE);

   rate.reset();
   sw.start();
}


void loop()
{
   float value = getValue();
   client.setValue(value);

   client.loop(); // Continuously poll for events and maintain connection

   if (sw.elapsedMillis() > 1000)
   {
      feather.setCursor(ratePos);
      feather.println(rate.getRate(), rateFormat, Color::VALUE);
      sw.reset();
   }
}
