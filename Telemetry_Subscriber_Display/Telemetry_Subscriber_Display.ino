
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>

#include <WebSocketsClient.h>
#include "Feather.h"
#include "SerialX.h"
#include "WiFiSettings.h"
#include "Stopwatch.h"
#include "RateTracker.h"

Feather feather;

const char* webSocketServerHost = "192.168.1.43"; // e.g., "192.168.1.100" or "example.com"
constexpr uint16_t webSocketServerPort = 5029;    // Port the server is listening on
const char* webSocketPath = "/";                  // WebSocket path, typically "/"

WiFiMulti WiFiMulti;
WebSocketsClient webSocket;
bool ready = false;
Stopwatch sw;
RollingRateTracker rate;
Point16 ratePos;
void(*resetFunc) (void) = 0;

Format rateFormat("###/s");

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length)
{
   switch (type)
   {
   case WStype_DISCONNECTED:
      Serial.printf("[WS] Disconnected!\n");
      resetFunc();
      break;

   case WStype_CONNECTED:
      if (rate.getCount() == 0)
      {
         feather.printlnR("OK", Color::VALUE);
         delay(1000);
         feather.clearDisplay();
      }
      rate.reset();
      webSocket.sendTXT("Subscribe ArduinoTest");

      // request the first value
      webSocket.sendTXT("\n");
      break;

   case WStype_TEXT:
      Serial.println((const char*) payload);
      // request the next value
      webSocket.sendTXT("\n");

      ready = true;
      break;

   case WStype_BIN:
      Serial.printf("[WS] Got Binary data\n");
      break;

   case WStype_PONG:
      break;

   default:
      Serial.print("Unhandled Socket Event Type: ");
      Serial.println(type);
      break;
   }
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
   webSocket.begin(webSocketServerHost, webSocketServerPort, webSocketPath);

   // Assign event handler
   webSocket.onEvent(webSocketEvent);

   // Set up ping/pong
   webSocket.setReconnectInterval(5000);
}

void loop()
{
   webSocket.loop(); // Continuously poll for events and maintain connection

   if (rate.getCount() == 0)
   {
      feather.setCursor(0, 0);
      feather.setTextSize(3);
      feather.println("Subscriber", Color::HEADING);

      feather.setTextSize(2);
      feather.moveCursorY(feather.charH() / 2);

      feather.print(" Topic: ", Color::LABEL);
      feather.println("ArduinoTest", Color::VALUE);
      feather.moveCursorY(1);

      feather.print("  Rate: ", Color::LABEL);
      ratePos = feather.getCursor();
      feather.println("---", Color::VALUE);
   }

   if (ready)
   {
      rate.tick();

      if (sw.elapsedMillis() > 1000)
      {
         feather.setCursor(ratePos);
         feather.println(rate.getRate(), rateFormat, Color::VALUE);
         sw.reset();
      }

      ready = false;
   }
}
