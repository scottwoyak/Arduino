//
// Logger
//
// Connects to WiFi and opens a WebSocket connection to the LogServer
// (C:\SourceCode\LogServer), either the local instance on the LAN or the remote
// DigitalOcean-hosted instance (see WiFiSettings.h), via the shared Logger class (see
// Logger.h). Once connected, Logger sends the initial JSON handshake identifying the
// device (deviceId/sketch/version) automatically, then this sketch sends a heartbeat
// text log message once per second. Also demonstrates handling a sketch-specific
// command ("Ping") sent from the LogServer, in addition to Logger's built-in
// "GetStatus" command.
//

// Uncomment to use the local LogServer instead of the remote one
#define LOG_SERVER_LOCAL

#include <string>

#include "ArduinoBoard.h"
#include "Logger.h"
#include "SerialX.h"
#include "Timer.h"
#include "WiFiSettings.h"

constexpr auto VERSION = "1.0";
constexpr auto SKETCH_NAME = "Logger";

constexpr float HEARTBEAT_PERIOD_S = 10.0f;

Arduino arduino;
TimerSecs heartbeatTimer(HEARTBEAT_PERIOD_S);

///
/// <summary>
/// Handles commands received from the LogServer that aren't one of Logger's built-in
/// commands ("GetStatus").
/// </summary>
/// <param name="command">Command text received from the LogServer.</param>
///
void onCommand(const char* command)
{
   if (strcasecmp(command, "Hi") == 0)
   {
      Logger.respond("Hello");
   }
   else
   {
      Serial.println((std::string("Unhandled command: ") + command).c_str());
   }
}

///
/// <summary>
/// Adds sketch-specific fields to a GetStatus reply, on top of Logger's base fields.
/// </summary>
/// <param name="status">The in-progress status to add fields to.</param>
///
void onStatus(LoggerStatus& status)
{
   status.add("Heartbeat Period", std::to_string(HEARTBEAT_PERIOD_S) + " secs");
}

void setup()
{
   SerialX::begin();

   arduino.begin();
   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD);

   Logger.begin(SKETCH_NAME, VERSION);
   Logger.onCommand(onCommand);
   Logger.onStatus(onStatus);
}

void loop()
{
   Logger.loop();

   if (Logger.isConnected() && heartbeatTimer.ready())
   {
      std::string message = "Heartbeat from " + std::string(SKETCH_NAME) + " at " + std::to_string(millis()) + "ms";
      Logger.log(message);
   }
}
