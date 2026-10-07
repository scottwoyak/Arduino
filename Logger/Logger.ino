//
// Logger
//
// Connects to WiFi and opens a WebSocket connection to the Device Server (the same
// server used for telemetry; see WiFiSettings.h and DeviceServerClient.h), trying the
// local Raspberry instance first and falling back to the public production instance,
// via the shared Logger class (see Logger.h). Once connected, Logger sends the initial
// JSON handshake identifying the device (deviceId/sketch/version) automatically, then
// this sketch sends a heartbeat log message once per second. Also demonstrates
// handling a sketch-specific command ("Hi") sent from the Device Server.
//

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
/// Handles commands received from the LogServer that aren't built-in commands.
/// </summary>
/// <param name="command">Command text received from the LogServer.</param>
///
void onCommand(const char* command)
{
   if (strcasecmp(command, "Hi") == 0)
   {
      DeviceServerClient::respond("Hello");
   }
   else
   {
      Serial.println((std::string("Unhandled command: ") + command).c_str());
   }
}

void setup()
{
   SerialX::begin();

   arduino.begin();
   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD);

   DeviceServerClient::begin(SKETCH_NAME, VERSION);
   DeviceServerClient::onCommand(onCommand);
}

void loop()
{
   DeviceServerClient::loop();

   if (DeviceServerClient::isConnected() && heartbeatTimer.ready())
   {
      std::string message = "Heartbeat from " + std::string(SKETCH_NAME) + " at " + std::to_string(millis()) + "ms";
      Logger.log(message);
   }
}
