//
// Logger
//
// Connects to WiFi and opens a WebSocket connection to the LogServer
// (C:\SourceCode\LogServer), either the local instance on the LAN or the remote
// DigitalOcean-hosted instance (see WiFiSettings.h). Once connected, sends an initial
// JSON handshake identifying the device (deviceId/sketch/version), then sends a
// heartbeat text log message once per second.
//

// Uncomment to use the local LogServer instead of the remote one
//#define LOG_SERVER_LOCAL

#include <string>

#include <WebSocketsClient.h>

#include "ArduinoBoard.h"
#include "SerialX.h"
#include "Timer.h"
#include "WiFiSettings.h"

constexpr auto VERSION = "v1.0";
constexpr auto SKETCH_NAME = "Logger";

constexpr float HEARTBEAT_PERIOD_S = 1.0f;

Arduino arduino;
WebSocketsClient webSocket;
TimerSecs heartbeatTimer(HEARTBEAT_PERIOD_S);
bool connected = false;

///
/// <summary>
/// Sends the initial JSON handshake message identifying this device to the LogServer.
/// </summary>
///
void sendHandshake()
{
   std::string deviceId = WiFi.macAddress().c_str();

   std::string message = "{\"DeviceId\":\"" + deviceId +
      "\",\"Sketch\":\"" + SKETCH_NAME +
      "\",\"Version\":\"" + VERSION + "\"}";

   webSocket.sendTXT(message.c_str());
   Serial.println(message.c_str());
}

///
/// <summary>
/// Handles WebSocket lifecycle events: sends the handshake on connect, and tracks the
/// connected state so the heartbeat loop only sends once the socket is up.
/// </summary>
/// <param name="type">The event type reported by WebSocketsClient.</param>
/// <param name="payload">The event payload, if any.</param>
/// <param name="length">The length of the payload, in bytes.</param>
///
void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length)
{
   switch (type)
   {
      case WStype_CONNECTED:
         connected = true;
         Serial.println("LogServer connected");
         sendHandshake();
         break;

      case WStype_DISCONNECTED:
         connected = false;
         Serial.println("LogServer disconnected");
         break;

      case WStype_ERROR:
      {
         std::string reason = (payload != nullptr && length > 0) ? std::string((const char*)payload, length) : "";
         Serial.println(("LogServer error: " + reason).c_str());
         break;
      }

      default:
         break;
   }
}

void setup()
{
   SerialX::begin();

   arduino.begin();
   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD);

   webSocket.onEvent(onWebSocketEvent);

#ifdef LOG_SERVER_LOCAL
   webSocket.begin(LOG_SERVER_HOST, LOG_SERVER_PORT, LOG_SERVER_PATH);
#else
   // The remote LogServer is only reachable over TLS (port 443); certificate
   // validation is skipped here since the WebSocketsClient library needs a pinned
   // fingerprint or CA cert to validate, which this sketch does not maintain.
   webSocket.beginSSL(LOG_SERVER_HOST, LOG_SERVER_PORT, LOG_SERVER_PATH);
#endif

   Serial.println("Connecting to LogServer...");
}

void loop()
{
   webSocket.loop();

   if (connected && heartbeatTimer.ready())
   {
      std::string message = "Heartbeat from " + std::string(SKETCH_NAME) + " at " + std::to_string(millis()) + "ms";
      webSocket.sendTXT(message.c_str());
      Serial.println(message.c_str());
   }
}
