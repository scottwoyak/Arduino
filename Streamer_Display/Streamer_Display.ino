
#define DEBUG_WEBSOCKETS(...) Serial.printf( __VA_ARGS__ )
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>

#include <WebSocketsClient.h> // Include the client library
#include "Feather.h"
#include "SerialX.h"
#include "WiFiSettings.h"

Feather feather;

const char* webSocketServerHost = "192.168.1.43"; // e.g., "192.168.1.100" or "example.com"
uint16_t webSocketServerPort = 5029;      // Port the server is listening on
const char* webSocketPath = "/ws";        // WebSocket path, typically "/"

/*/
const char* webSocketServerHost = "echo.websocket.org"; // e.g., "192.168.1.100" or "example.com"
uint16_t webSocketServerPort = 80;      // Port the server is listening on
const char* webSocketPath = "/";        // WebSocket path, typically "/"
*/

WiFiMulti WiFiMulti;
WebSocketsClient webSocketClient;

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length)
{
   switch (type)
   {
   case WStype_DISCONNECTED:
      Serial.printf("[WS] Disconnected!\n");
      break;
   case WStype_CONNECTED:
      Serial.printf("[WS] Connected to url: %s\n", payload);
      // Send message when connected
      webSocketClient.sendTXT("Hello from ESP32 Client");
      break;
   case WStype_TEXT:
      Serial.printf("[WS] Got Text: %s\n", payload);
      break;
   case WStype_BIN:
      Serial.printf("[WS] Got Binary data\n");
      break;

   WStype_ERROR:
      Serial.println("WStype_ERROR");
      break;

   WStype_FRAGMENT_TEXT_START:
      Serial.println("WStype_TEXT_START");
      break;

   WStype_FRAGMENT_BIN_START:
      Serial.println("WStype_BIN_START");
      break;

   WStype_FRAGMENT:
      Serial.println("WStype_FRAGMENT");
      break;

   WStype_FRAGMENT_FIN:
      Serial.println("WStype_FRAGMENT_FIN");
      break;

   WStype_PING:
      Serial.println("WStype_PING");
      break;

   WStype_PONG:
      Serial.println("WStype_PONG");
      break;

   default:
      Serial.print("WStype = ");
      Serial.println(type);
      break;
   }
}

void setup()
{
   SerialX::begin();
   feather.begin();

   // Connect to WiFi
   /*
   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
   while (WiFi.status() != WL_CONNECTED)
   {
      delay(500);
      Serial.print(".");
   }
   */
   WiFiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

   DEBUG_WEBSOCKETS("Hello");
   Serial.print("Connecting to WiFi");
   while (WiFiMulti.run() != WL_CONNECTED)
   {
      delay(100);
      Serial.print(".");
   }
   Serial.println();

   Serial.println("WiFi connected");

   // Connect to WebSocket server
   Serial.print("webSocketClient.begin ");
   Serial.print(webSocketServerHost);
   Serial.print(" ");
   Serial.print(webSocketServerPort);
   Serial.print(" ");
   Serial.print(webSocketPath);
   Serial.println();

   webSocketClient.begin(webSocketServerHost, webSocketServerPort, webSocketPath);

   // Assign event handler
   webSocketClient.onEvent(webSocketEvent);

   // Set up ping/pong
   webSocketClient.setReconnectInterval(5000);
}

void loop()
{
   webSocketClient.loop(); // Continuously poll for events and maintain connection
}
