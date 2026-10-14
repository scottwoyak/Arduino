#pragma once

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/gpio.h>

#include "ArduinoBoardId.h"
#include "DeviceHealth.h"
#include "Format.h"
#include "OTAUpdater.h"
#include "Timer.h"
#include "WebSocketJsonClient.h"
#include "WebSocketStream.h"
#include "WiFiSettings.h"

///
/// <summary>
/// Severity of a log message sent to the Device Server. The severity is sent as the
/// message's "level" field; it is never added to the message text.
/// </summary>
///
enum class LogSeverity
{
   DEBUG,
   INFO,
   WARN,
   ERROR
};

///
/// <summary>
/// WebSocket client for the DeviceServer
/// see TelemetryClient.h), used to send log messages and respond to device-management
/// commands. Unlike the old LogServer protocol (plain text frames), every message
/// exchanged with the DeviceServer is a JSON object.
/// </summary>
/// <remarks>
/// Protocol (JSON text frames), reusing the DeviceServer's device role/token:
///   Handshake, sent on connect:  {"role":"device","token":"...","deviceId":"...","sketch":"...","version":"...","site":"...","location":"..."}
///   Server reply:                {"type":"ack"}
///   Log message (device to hub): {"type":"log","level":"Info","tags":"wifi,reconnect","message":"..."}
///   Command (hub to device):     {"type":"command","action":"readPin 14"}
///   Command response:            {"type":"response","message":"..."}
///   Shell output (device to hub):{"type":"shellOutput","text":"..."}
///   Shell input (hub to device): {"type":"shellInput","text":"..."}
/// Commands are plain text, like a shell: a verb optionally followed by space-separated
/// arguments (e.g. "readPin 14"), rather than a separate JSON payload object. Send "Help"
/// to get a list of all commands understood by the device (built-in plus any registered
/// via onCommand()).
/// The shell channel is a separate, independent raw text channel (see onShellLine()/
/// shell()) that mimics Serial: "shellInput" text is delivered to a single registered
/// handler and appended to a Stream-like buffer, and text written to shell() is sent
/// out as "shellOutput". It is never mixed with the command/response channel; a shell
/// session requires no handshake and is simply available for the life of the connection.
/// A log entry built up via logPartial()/log() sends one JSON message per fragment:
///   {"type":"log","level":"Info","tags":"...","message":"Sensor... ","continued":true}
///   {"type":"log","message":"warming up... ","append":true,"continued":true}
///   {"type":"log","message":"OK","append":true}
/// Connects to the local Raspberry DeviceServer first, falling back to the public
/// (production) DeviceServer if the primary isn't reachable within FAILOVER_TIMEOUT_MS.
/// The DeviceServer is a separate server/process from the TelemetryServer (see
/// TelemetryClient.h) - a different .NET service with its own port - though both happen
/// to use the same "/ws" WebSocket path convention.
/// Connection is best-effort: a dropped/failed connection is logged to Serial but does
/// not reset the device, since log delivery shouldn't be able to crash a sketch that
/// otherwise works fine. All members are static since a sketch has a single DeviceServer
/// log connection; use the global Logger instance (see Logger.h) rather than this class
/// directly.
/// </remarks>
///
class DeviceServerClient
{
   static constexpr uint32_t HEARTBEAT_PING_MS = 30000;
   static constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 10000;
   static constexpr uint8_t HEARTBEAT_FAILURES = 2;

   ///
   /// <summary>
   /// Instance-side connection, built on the shared WebSocketJsonClient plumbing
   /// (connect/failover, reconnect/heartbeat, JSON framing). DeviceServerClient itself
   /// stays a static-only API (see class remarks), so this single instance is held in
   /// a static member below.
   /// </summary>
   ///
   class _Connection : public WebSocketJsonClient
   {
   protected:
      void _onConnected() override
      {
         DeviceServerClient::_debugPrint("Connected to " + getUrl() + ", sending handshake");
         DeviceServerClient::_sendHandshake();
      }

      void _onDisconnected(const std::string& reason, bool wasConnected) override
      {
         DeviceServerClient::_debugPrint("Disconnected: " + (reason.empty() ? std::string("(no reason given)") : reason));

         if (!DeviceServerClient::_outageActive)
         {
            DeviceServerClient::_outageActive = true;
            DeviceServerClient::_outageStartMs = millis();
         }

         if (!DeviceServerClient::_everConnected)
         {
            // Failing on the primary endpoint isn't a failure yet; the fallback may still connect.
            if (!DeviceServerClient::_initialFailureLogged && hasTriedAllEndpoints())
            {
               DeviceServerClient::_initialFailureLogged = true;
               DeviceServerClient::log(reason.empty() ? "FAILED" : std::string("FAILED: ") + reason);
            }
         }
         else
         {
            // WebSocketsClient reconnects on its own almost immediately in the common
            // case, so hold off logging until the grace period expires (see loop()).
            DeviceHealth::hubReconnects++;
            DeviceServerClient::_pendingDisconnectReason = reason.empty() ? "unknown" : reason;
            DeviceServerClient::_disconnectPending = true;
            DeviceServerClient::_disconnectStartMs = millis();
            if (!DeviceServerClient::_offline)
            {
               DeviceServerClient::_offline = true;
               DeviceServerClient::_offlineSinceMs = millis();
            }
         }
         (void)wasConnected;
      }

      void _onJsonMessage(JsonDocument& doc) override
      {
         std::string received;
         serializeJson(doc, received);
         DeviceServerClient::_debugPrint("Received: " + received);

         const char* type = doc["type"];
         if (type == nullptr)
         {
            return;
         }

         if (strcmp(type, "ack") == 0)
         {
            _ready = true;
            DeviceServerClient::_outageActive = false;
            DeviceServerClient::_flushPendingMessages();
            if (!DeviceServerClient::_everConnected)
            {
               DeviceServerClient::_everConnected = true;
               // "Direct" vs "OK" lets you tell at a glance (via serial/display) whether the
               // connection went straight to the local/LAN server or had to fall back to the
               // public (e.g. Cloudflare) endpoint - see isDirectConnection(). Reported here,
               // once the handshake is actually acknowledged, rather than in _onConnected(),
               // since a plain WebSocket connect can succeed even if the handshake itself is
               // then rejected/ignored by the server (e.g. a bad token).
               DeviceServerClient::log(isDirectConnection() ? getHost() : "OK");

                // A freshly booted device has no state (or only a connecting state), which would
                // leave the server showing the previous one (e.g. RESTARTING). Being acknowledged
                // by the hub means the device is up.
                if (DeviceServerClient::_state.empty() || DeviceServerClient::_state == "STARTED" || DeviceServerClient::_state == "WIFI_CONNECTING" || DeviceServerClient::_state == "WEB_CONNECTING")
                {
                   DeviceServerClient::setState("RUNNING");
                }
            }
            else if (DeviceServerClient::_disconnectLogged)
            {
                     DeviceServerClient::log("Logging reconnected after " + std::to_string((millis() - DeviceServerClient::_offlineSinceMs) / 1000) + " s");
                  }
                  DeviceServerClient::_offline = false;
                  DeviceServerClient::_disconnectPending = false;
                  DeviceServerClient::_disconnectLogged = false;
         }
         else if (strcmp(type, "command") == 0)
         {
            const char* command = doc["action"] | doc["command"].as<const char*>();
            if (command == nullptr)
            {
               return;
            }

            if (strcasecmp(command, "Update") == 0)
            {
               const char* url = doc["payload"]["url"];
               if (url == nullptr)
               {
                  DeviceServerClient::log("Update command missing payload url");
                  return;
               }

               DeviceServerClient::respond("Updating firmware");

               // Behind a TLS proxy the server sees plain http and builds an http:// URL,
               // so upgrade it when we reached the hub over TLS.
               std::string downloadUrl = url;
               if (DeviceServerClient::_connection.isTls() && downloadUrl.rfind("http://", 0) == 0)
               {
                  downloadUrl.insert(4, "s");
               }
               OTAUpdater::requestActiveUpdate(downloadUrl);
               return;
            }

            DeviceServerClient::_handleCommand(command);
         }
         else if (strcmp(type, "shellInput") == 0)
         {
            const char* text = doc["text"];
            if (text != nullptr)
            {
               DeviceServerClient::_handleShellInput(text);
            }
         }
      }

      void _onError(const std::string& reason) override
      {
         DeviceServerClient::_debugPrint("Error: " + reason);
         DeviceServerClient::log("WebSocket error: " + reason, LogSeverity::ERROR);
      }
   };

   /// <summary>How long (in milliseconds) DeviceServerClient waits after a disconnect before logging "Logging disconnected: ...".</summary>
   static constexpr uint32_t DISCONNECT_GRACE_MS = 3000UL;

   /// <summary>Minutes the hub connection may stay down (dropped or never connected) before the device resets.</summary>
   static constexpr uint32_t OUTAGE_RESET_M = 10UL;

   /// <summary>Seconds Util::reset() waits before restarting after a prolonged hub outage.</summary>
   static constexpr float OUTAGE_RESET_DELAY_S = 10.0f;

   /// <summary>True while the hub connection is down and hasn't yet been acknowledged again.</summary>
   static inline bool _outageActive = false;

   /// <summary>millis() timestamp of the start of the current hub outage.</summary>
   static inline unsigned long _outageStartMs = 0;

   static inline _Connection _connection;
   static inline bool _everConnected = false;
   static inline bool _initialFailureLogged = false;
   static inline std::string _sketchName;
   static constexpr const char* BUILD_TIME = "BUILD@" __DATE__ " " __TIME__;
   static inline std::string _version;
   static inline std::string _site;
   static inline std::string _location;
   /// <summary>The recorded reason for the previous shutdown (e.g. "Firmware Update"), set via setLastShutdownReason(); empty if none was recorded.</summary>
   static inline std::string _lastShutdownReason;

    /// <summary>A log message queued while disconnected
   struct _PendingMessage
   {
      std::string json;
      uint32_t occurredMs;
   };

   /// <summary>Messages logged before the connection was up, sent once it completes. Each entry's json is a fully-formed JSON log message.</summary>
   static inline std::vector<_PendingMessage> _pendingMessages;

   /// <summary>True from the first disconnect until the connection is acknowledged again.</summary>
   static inline bool _offline = false;

   /// <summary>millis() timestamp of the first disconnect of the current outage.</summary>
   static inline uint32_t _offlineSinceMs = 0;

   /// <summary>Milliseconds to back-date the next queued message by (e.g. the disconnect grace period).</summary>
   static inline uint32_t _backdateMs = 0;

   /// <summary>True while waiting to see if a disconnect resolves itself within the grace period.</summary>
   static inline bool _disconnectPending = false;

   /// <summary>True once the current disconnect has actually been logged (so a matching "reconnected" is logged too).</summary>
   static inline bool _disconnectLogged = false;

   /// <summary>Reason text captured from the disconnect event, logged if the grace period expires.</summary>
   static inline std::string _pendingDisconnectReason;

   /// <summary>millis() timestamp of the most recent disconnect, used to time the grace period.</summary>
   static inline unsigned long _disconnectStartMs = 0;

   /// <summary>True if a logPartial() call is still awaiting its completing log() call.</summary>
   static inline bool _linePending = false;

   /// <summary>True if detailed DeviceServer communication (connect/disconnect/send/receive) is echoed to Serial; see enableDebug().</summary>
   static inline bool _debugEnabled = false;

   /// <summary>Optional sketch-supplied handler for commands not recognized as built-in (see onCommand()).</summary>
   static inline void (*_commandHandler)(const char* command) = nullptr;

   /// <summary>Optional sketch-supplied handler for raw shell input lines (see onShellLine()).</summary>
   static inline void (*_shellHandler)(const char* line) = nullptr;

   /// <summary>Serial-like stream backed by the "shellInput"/"shellOutput" messages; see shell().</summary>
   static inline WebSocketStream _shell;

   /// <summary>True while a line received over the shell channel is being dispatched through
   /// _handleCommand(), so respond() routes its reply to "shellOutput" instead of "response".</summary>
   static inline bool _inShellCommand = false;

   /// <summary>When set (via promptShellLine()), the next line received over the shell
   /// channel is treated as the answer to a pending interactive prompt instead of being
   /// dispatched as a command.</summary>
   static inline void (*_pendingPromptCallback)(const char* line) = nullptr;

   ///
   /// <summary>
   /// Describes one command for the built-in "Help" listing: its usage (verb plus any
   /// arguments, e.g. "readPin <pin>") and a short description of what it does.
   /// </summary>
   ///
public:
   struct CommandInfo
   {
      const char* usage;
      const char* description;
   };

private:
   /// <summary>Built-in commands, listed by "Help" first, in this order.</summary>
   static constexpr CommandInfo _BUILTIN_COMMANDS[] = {
      { "Help [-l] [-b|-s]", "Lists all commands understood by this device (-l for a brief usage-only listing, -b for only built-in commands, -s for only commands registered by the sketch)" },
      { "Restart", "Restarts the device" },
      { "GetHealth", "Reports WiFi/CPU/memory health as a JSON message" },
      { "Locate", "Asks the device to do something visibly identifiable" },
      { "I2CScan", "Scans the I2C bus and lists responding addresses" },
      { "I2CRead <address> <reg>", "Reads a register from an I2C device" },
      { "I2CWrite <address> <reg> <value>", "Writes a value to a register on an I2C device" },
      { "ReadPin [pin]", "Reads the digital state of a GPIO pin, or all pins if omitted" },
      { "WritePin <pin> <HIGH|LOW>", "Sets a GPIO pin's digital output state" },
      { "GetPinMode [pin]", "Reports whether a GPIO pin is configured as INPUT or OUTPUT, or all pins if omitted" },
      { "SetPinMode <pin> <INPUT|OUTPUT|INPUT_PULLUP>", "Sets a GPIO pin's mode" },
      { "AnalogRead [pin]", "Reads the analog (ADC) value of a pin, or all analog-capable pins if omitted" },
      { "GetUptime", "Reports how long the device has been running since boot" },
      { "GetInfo", "Reports the sketch name, version, board type, build time, uptime, IP address, and site/location" },
      { "GetWifiInfo", "Reports the current WiFi SSID, signal strength, and IP address" },
      { "GetResetReason", "Reports the reason the device last reset/rebooted" },
      { "GetMemory", "Reports free heap and largest allocatable block" },
      { "GetCpuInfo", "Reports chip model/revision/cores, CPU frequency, flash size, and internal temperature" },
      { "GetVersion", "Reports the sketch's firmware version" },
   };

   /// <summary>Extra commands registered by the sketch via onCommand(), listed by "Help" after the built-ins.</summary>
   static inline const CommandInfo* _sketchCommands = nullptr;

   /// <summary>Number of entries in _sketchCommands.</summary>
   static inline size_t _sketchCommandCount = 0;

   ///
   /// <summary>
   /// Splits a command string into its verb and arguments, e.g. "readPin 14" into
   /// "readPin" and {"14"}.
   /// </summary>
   /// <param name="command">Full command text received from the DeviceServer.</param>
   /// <param name="args">Receives the space-separated arguments following the verb.</param>
   /// <returns>The command verb (leading token).</returns>
   ///
   static std::string _parseCommand(const char* command, std::vector<std::string>& args)
   {
      std::istringstream stream(command);
      std::string verb;
      stream >> verb;

      std::string arg;
      while (stream >> arg)
      {
         args.push_back(arg);
      }

      return verb;
   }

   ///
   /// <summary>
   /// Builds and sends the response text for the "Help" command: usage and description
   /// of every built-in command, followed by any sketch-registered commands. When
   /// listOnly is true ("Help -l"), only the usage of each command is listed, without
   /// its description. When builtinOnly is true ("Help -b"), only built-in commands are
   /// listed. When sketchOnly is true ("Help -s"), only sketch-registered commands are
   /// listed. builtinOnly and sketchOnly are mutually exclusive; if both are set, sketchOnly wins.
   /// </summary>
   /// <param name="listOnly">If true, lists only command usage, omitting descriptions.</param>
   /// <param name="builtinOnly">If true, omits sketch-registered commands and lists only built-in ones.</param>
   /// <param name="sketchOnly">If true, omits built-in commands and lists only sketch-registered ones.</param>
   ///
   static void _respondHelp(bool listOnly, bool builtinOnly, bool sketchOnly)
   {
      std::ostringstream text;
      if (!sketchOnly)
      {
         for (const CommandInfo& info : _BUILTIN_COMMANDS)
         {
            text << info.usage;
            if (!listOnly)
            {
               text << " - " << info.description;
            }
            text << "\n";
         }
      }

      if (!builtinOnly)
      {
         for (size_t i = 0; i < _sketchCommandCount; i++)
         {
            text << _sketchCommands[i].usage;
            if (!listOnly)
            {
               text << " - " << _sketchCommands[i].description;
            }
            text << "\n";
         }
      }

      std::string message = text.str();
      if (!message.empty())
      {
         message.pop_back();
      }

      respond(message.c_str());
   }

   ///
   /// <summary>
   /// Scans the I2C bus for responding devices and returns a text summary.
   /// </summary>
   /// <returns>A description of the addresses found, or a "no devices" message.</returns>
   ///
   static std::string _i2cScan()
   {
      std::ostringstream text;
      uint8_t found = 0;

      for (uint8_t address = 1; address < 127; address++)
      {
         Wire.beginTransmission(address);
         if (Wire.endTransmission() == 0)
         {
            if (found > 0)
            {
               text << ", ";
            }

            text << "0x" << std::hex << std::uppercase << static_cast<int>(address);
            found++;
         }
      }

      if (found == 0)
      {
         return "No I2C devices found";
      }

      std::ostringstream result;
      result << "Found " << std::dec << static_cast<int>(found) << " I2C device(s): " << text.str();
      return result.str();
   }

   ///
   /// <summary>
   /// Reads and reports the digital state of a GPIO pin. If no pin is specified, reports
   /// the state of every valid GPIO pin on the board instead.
   /// </summary>
   /// <param name="args">Command arguments; args[0] is the pin number, or omitted to read all pins.</param>
   /// <returns>A description of the pin's state(s).</returns>
   ///
   static std::string _readPin(const std::vector<std::string>& args)
   {
      if (args.empty())
      {
         std::ostringstream result;
         for (uint8_t pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
         {
            if (!digitalPinIsValid(pin))
            {
               continue;
            }

            if (result.tellp() > 0)
            {
               result << "\n";
            }
            result << "Pin " << static_cast<int>(pin) << " = " << (digitalRead(pin) == HIGH ? "HIGH" : "LOW");
         }
         return result.str();
      }

      int pin = atoi(args[0].c_str());
      std::ostringstream result;
      result << "Pin " << pin << " = " << (digitalRead(pin) == HIGH ? "HIGH" : "LOW");
      return result.str();
   }

   ///
   /// <summary>
   /// Reports whether a GPIO pin is currently configured as INPUT or OUTPUT. Based on the
   /// ESP32 GPIO output-enable registers, so it can't distinguish INPUT from INPUT_PULLUP/
   /// INPUT_PULLDOWN. If no pin is specified, reports the mode of every valid GPIO pin on
   /// the board instead.
   /// </summary>
   /// <param name="args">Command arguments; args[0] is the pin number, or omitted to report all pins.</param>
   /// <returns>A description of the pin's configured mode(s).</returns>
   ///
   static std::string _getPinMode(const std::vector<std::string>& args)
   {
      if (args.empty())
      {
         std::ostringstream result;
         for (uint8_t pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
         {
            if (!digitalPinIsValid(pin))
            {
               continue;
            }

            gpio_io_config_t ioConfig = {};
            gpio_get_io_config(static_cast<gpio_num_t>(pin), &ioConfig);
            bool isOutput = ioConfig.oe;

            if (result.tellp() > 0)
            {
               result << "\n";
            }
            result << "Pin " << static_cast<int>(pin) << " = " << (isOutput ? "OUTPUT" : "INPUT");
         }
         return result.str();
      }

      int pin = atoi(args[0].c_str());

      gpio_io_config_t ioConfig = {};
      gpio_get_io_config(static_cast<gpio_num_t>(pin), &ioConfig);
      bool isOutput = ioConfig.oe;

      std::ostringstream result;
      result << "Pin " << pin << " = " << (isOutput ? "OUTPUT" : "INPUT");
      return result.str();
   }

   ///
   /// <summary>
   /// Sets a GPIO pin's digital output state.
   /// </summary>
   /// <param name="args">Command arguments; args[0] is the pin number, args[1] is HIGH or LOW.</param>
   /// <returns>A description of the result, or an error message if an argument is missing/invalid.</returns>
   ///
   static std::string _writePin(const std::vector<std::string>& args)
   {
      if (args.size() < 2)
      {
         return "WritePin requires a pin number and HIGH/LOW, e.g. \"WritePin 14 HIGH\"";
      }

      int pin = atoi(args[0].c_str());
      if (strcasecmp(args[1].c_str(), "HIGH") == 0)
      {
         digitalWrite(pin, HIGH);
      }
      else if (strcasecmp(args[1].c_str(), "LOW") == 0)
      {
         digitalWrite(pin, LOW);
      }
      else
      {
         return "WritePin's second argument must be HIGH or LOW";
      }

      std::ostringstream result;
      result << "Pin " << pin << " set to " << args[1];
      return result.str();
   }

   ///
   /// <summary>
   /// Sets a GPIO pin's mode (INPUT, OUTPUT, or INPUT_PULLUP).
   /// </summary>
   /// <param name="args">Command arguments; args[0] is the pin number, args[1] is the mode.</param>
   /// <returns>A description of the result, or an error message if an argument is missing/invalid.</returns>
   ///
   static std::string _setPinMode(const std::vector<std::string>& args)
   {
      if (args.size() < 2)
      {
         return "SetPinMode requires a pin number and mode, e.g. \"SetPinMode 14 OUTPUT\"";
      }

      int pin = atoi(args[0].c_str());
      uint8_t mode;
      if (strcasecmp(args[1].c_str(), "INPUT") == 0)
      {
         mode = INPUT;
      }
      else if (strcasecmp(args[1].c_str(), "OUTPUT") == 0)
      {
         mode = OUTPUT;
      }
      else if (strcasecmp(args[1].c_str(), "INPUT_PULLUP") == 0)
      {
         mode = INPUT_PULLUP;
      }
      else
      {
         return "SetPinMode's second argument must be INPUT, OUTPUT, or INPUT_PULLUP";
      }

      pinMode(pin, mode);

      std::ostringstream result;
      result << "Pin " << pin << " mode set to " << args[1];
      return result.str();
   }

   ///
   /// <summary>
   /// Reads the analog (ADC) value of a pin. If no pin is specified, reads every valid
   /// analog-capable GPIO pin on the board instead.
   /// </summary>
   /// <param name="args">Command arguments; args[0] is the pin number, or omitted to read all analog pins.</param>
   /// <returns>A description of the pin's analog reading(s).</returns>
   ///
   static std::string _analogRead(const std::vector<std::string>& args)
   {
      if (args.empty())
      {
         std::ostringstream result;
         for (uint8_t pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
         {
            if (!digitalPinIsValid(pin) || digitalPinToAnalogChannel(pin) < 0)
            {
               continue;
            }

            if (result.tellp() > 0)
            {
               result << "\n";
            }
            result << "Pin " << static_cast<int>(pin) << " = " << analogRead(pin);
         }
         return result.str();
      }

      int pin = atoi(args[0].c_str());
      std::ostringstream result;
      result << "Pin " << pin << " = " << analogRead(pin);
      return result.str();
   }

   ///
   /// <summary>
   /// Reads a register from an I2C device.
   /// </summary>
   /// <param name="args">Command arguments; args[0] is the I2C address, args[1] is the register.</param>
   /// <returns>A description of the register's value, or an error message if an argument is missing/invalid.</returns>
   ///
   static std::string _i2cRead(const std::vector<std::string>& args)
   {
      if (args.size() < 2)
      {
         return "I2CRead requires an address and register, e.g. \"I2CRead 0x23 0x10\"";
      }

      uint8_t address = static_cast<uint8_t>(strtol(args[0].c_str(), nullptr, 0));
      uint8_t reg = static_cast<uint8_t>(strtol(args[1].c_str(), nullptr, 0));

      Wire.beginTransmission(address);
      Wire.write(reg);
      if (Wire.endTransmission(false) != 0)
      {
         return "I2CRead failed: no response from device at that address";
      }

      if (Wire.requestFrom(address, (uint8_t)1) != 1)
      {
         return "I2CRead failed: no data returned";
      }

      uint8_t value = Wire.read();
      std::ostringstream result;
      result << "0x" << std::hex << std::uppercase << static_cast<int>(address) << " reg 0x" << static_cast<int>(reg) << " = 0x" << static_cast<int>(value);
      return result.str();
   }

   ///
   /// <summary>
   /// Writes a value to a register on an I2C device.
   /// </summary>
   /// <param name="args">Command arguments; args[0] is the I2C address, args[1] is the register, args[2] is the value.</param>
   /// <returns>A description of the result, or an error message if an argument is missing/invalid.</returns>
   ///
   static std::string _i2cWrite(const std::vector<std::string>& args)
   {
      if (args.size() < 3)
      {
         return "I2CWrite requires an address, register, and value, e.g. \"I2CWrite 0x23 0x10 0x01\"";
      }

      uint8_t address = static_cast<uint8_t>(strtol(args[0].c_str(), nullptr, 0));
      uint8_t reg = static_cast<uint8_t>(strtol(args[1].c_str(), nullptr, 0));
      uint8_t value = static_cast<uint8_t>(strtol(args[2].c_str(), nullptr, 0));

      Wire.beginTransmission(address);
      Wire.write(reg);
      Wire.write(value);
      if (Wire.endTransmission() != 0)
      {
         return "I2CWrite failed: no response from device at that address";
      }

      std::ostringstream result;
      result << "0x" << std::hex << std::uppercase << static_cast<int>(address) << " reg 0x" << static_cast<int>(reg) << " set to 0x" << static_cast<int>(value);
      return result.str();
   }

   ///
   /// <summary>
   /// Reports how long the device has been running since boot.
   /// </summary>
   /// <returns>A description of the uptime in seconds.</returns>
   ///
   static std::string _getUptime()
   {
      std::ostringstream result;
      result << "Uptime: " << formatDuration(millis()).c_str();
      return result.str();
   }

   ///
   /// <summary>
   /// Reports the sketch name, version, board variant, build time, uptime, IP
   /// address, and resolved site/location (if any), for identifying what's running
   /// on a device at a glance.
   /// </summary>
   /// <returns>A description of the sketch/board identity.</returns>
   ///
   static std::string _getInfo()
   {
      std::ostringstream result;
      result << "Sketch: " << (_sketchName.empty() ? "(unknown)" : _sketchName) << "\n"
             << "Version: " << (_version.empty() ? "(unknown)" : _version) << "\n"
#if defined(ARDUINO_BOARD_VARIANT_ID)
             << "Board: " << ARDUINO_BOARD_VARIANT_ID << "\n"
#else
             << "Board: " << ARDUINO_BOARD << "\n"
#endif
             << "Build time: " << BUILD_TIME << "\n"
             << "Uptime: " << formatDuration(millis()).c_str() << "\n"
             << "IP address: " << (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "(not connected)");

      if (!_site.empty() || !_location.empty())
      {
         result << "\nSite: " << (_site.empty() ? "(none)" : _site) << ", Location: " << (_location.empty() ? "(none)" : _location);
      }

      return result.str();
   }

   ///
   /// <summary>
   /// Reports the current WiFi SSID, signal strength, and IP address.
   /// </summary>
   /// <returns>A description of the WiFi connection.</returns>
   ///
   static std::string _getWifiInfo()
   {
      // Uses the configured SSID (WIFI_SSID) rather than WiFi.SSID(), which queries the
      // driver's AP info synchronously and has been observed to briefly disrupt the WiFi
      // connection; RSSI()/localIP() are cheap local reads and don't have this problem.
      std::ostringstream result;
      result << "SSID: " << WIFI_SSID << "\n"
             << "RSSI: " << WiFi.RSSI() << " dBm\n"
             << "IP: " << WiFi.localIP().toString().c_str();
      return result.str();
   }

   ///
   /// <summary>
   /// Reports the reason the device last reset/rebooted.
   /// </summary>
   /// <returns>A description of the reset reason.</returns>
   ///
   static std::string _getResetReason()
   {
      std::ostringstream result;
      result << "Reset reason: " << (_lastShutdownReason.empty() ? Util::resetReasonString() : _lastShutdownReason.c_str());
      return result.str();
   }

   ///
   /// <summary>
   /// Reports heap, PSRAM (if present), and flash usage diagnostics.
   /// </summary>
   /// <returns>A description of the device's current memory usage.</returns>
   ///
   static std::string _getMemory()
   {
      std::ostringstream result;
      result << "Free heap: " << formatBytes(ESP.getFreeHeap()).c_str() << " / " << formatBytes(ESP.getHeapSize()).c_str() << "\n"
             << "Min free heap seen: " << formatBytes(ESP.getMinFreeHeap()).c_str() << "\n"
             << "Largest allocatable block: " << formatBytes(ESP.getMaxAllocHeap()).c_str() << "\n"
             << "Sketch size: " << formatBytes(ESP.getSketchSize()).c_str() << ", free: " << formatBytes(ESP.getFreeSketchSpace()).c_str();

      if (ESP.getPsramSize() > 0)
      {
         result << "\nFree PSRAM: " << formatBytes(ESP.getFreePsram()).c_str() << " / " << formatBytes(ESP.getPsramSize()).c_str();
      }

      return result.str();
   }

   ///
   /// <summary>
   /// Reports chip model/revision/cores, CPU frequency, flash size, and internal
   /// temperature sensor reading.
   /// </summary>
   /// <returns>A description of the device's CPU/chip diagnostics.</returns>
   ///
   static std::string _getCpuInfo()
   {
      std::ostringstream result;
      result << "Chip: " << ESP.getChipModel() << " rev " << ESP.getChipRevision() << ", " << (int)ESP.getChipCores() << " core(s)\n"
             << "CPU frequency: " << ESP.getCpuFreqMHz() << " MHz\n"
             << "Flash size: " << formatBytes(ESP.getFlashChipSize()).c_str() << "\n"
             << "Internal temperature: " << temperatureRead() << " C";
      return result.str();
   }

   ///
   /// <summary>
   /// Reports the sketch's firmware version.
   /// </summary>
   /// <returns>The sketch's version string.</returns>
   ///
   static std::string _getVersion()
   {
      return _version.empty() ? "No version set" : _version;
   }

   /// <summary>Handler invoked by the built-in "Locate" command (see onLocate()).</summary>
   static inline void (*_locateHandler)() = nullptr;

   /// <summary>Most recent device state reported via setState(); empty until the first call.</summary>
   static inline std::string _state;

   /// <summary>Milliseconds reportRestarting() waits for the state message to be transmitted before the restart.</summary>
   static constexpr uint32_t RESTART_FLUSH_MS = 100;

   /// <summary>Current default tags, sent with every log() / logPartial() call that doesn't specify its own tags (see setTag()/setTags()). Starts as {"Initializing"}, covering the sketch's boot/init sequence until logInitializationComplete() clears it.</summary>
   static inline std::vector<std::string> _tags = { "Initializing" };

   ///
   /// <summary>
   /// Sends a log fragment to the DeviceServer if connected, or queues it to be sent once
   /// the connection completes. Used by logPartial()/log() to send message fragments
   /// that the DeviceServer joins into a single entry: the first fragment has
   /// "continued":true, later ones add "append":true, and the last one omits "continued".
   /// </summary>
   /// <param name="message">Message fragment text.</param>
   /// <param name="severity">Severity of the message.</param>
   /// <param name="tags">Tags/components the message is associated with (see setTag()/setTags()).</param>
   /// <param name="final">True if this fragment completes the log entry.</param>
   ///
   static void _sendOrQueueLog(const std::string& message, LogSeverity severity, const std::vector<std::string>& tags, bool final)
   {
      // A fragment continues an earlier logPartial() entry if one is still pending. Only the
      // first fragment of an entry carries the level/tags; later ones set "append":true.
      bool append = _linePending;

      JsonDocument doc;
      doc["type"] = "log";
      if (!append)
      {
         doc["level"] = _levelName(severity);
         doc["tags"] = _joinTags(tags);
      }
      doc["message"] = message;
      if (append)
      {
         doc["append"] = true;
      }
      if (!final)
      {
         doc["continued"] = true;
      }

      std::string json;
      serializeJson(doc, json);

      if (_connection.isConnected())
      {
         _debugPrint("Sent: " + json);
         _connection.sendRaw(json);
      }
      else
      {
         _debugPrint("Queued (not connected): " + json);
         _pendingMessages.push_back({ json, millis() - _backdateMs });
      }
   }

   ///
   /// <summary>
   /// Maps a LogSeverity to the capitalized level name sent to the DeviceServer (e.g.
   /// "Info", matching the server's Envelope.Level field).
   /// </summary>
   /// <param name="severity">Severity to map.</param>
   /// <returns>The level name ("Debug", "Info", "Warn", or "Error").</returns>
   ///
   static const char* _levelName(LogSeverity severity)
   {
      switch (severity)
      {
         case LogSeverity::DEBUG:
            return "Debug";
         case LogSeverity::WARN:
            return "Warn";
         case LogSeverity::ERROR:
            return "Error";
         case LogSeverity::INFO:
         default:
            return "Info";
      }
   }

   ///
   /// <summary>
   /// Joins tags into the comma-separated string the DeviceServer expects on the wire (see
   /// TagsParser.Join() server-side).
   /// </summary>
   /// <param name="tags">Tags to join.</param>
   /// <returns>Comma-separated tags, or an empty string if there are none.</returns>
   ///
   static std::string _joinTags(const std::vector<std::string>& tags)
   {
      std::string result;
      for (size_t i = 0; i < tags.size(); i++)
      {
         if (i > 0)
         {
            result += ",";
         }
         result += tags[i];
      }
      return result;
   }

   ///
   /// <summary>
   /// Prints a DeviceServer communication debug message to Serial, if enabled via
   /// enableDebug(). Used to trace connects/disconnects and sent/received messages
   /// without cluttering normal Serial output (and without being sent to the DeviceServer
   /// itself, unlike log()).
   /// </summary>
   /// <param name="message">Debug message text.</param>
   ///
   static void _debugPrint(const std::string& message)
   {
      if (_debugEnabled)
      {
         Serial.print("DeviceServer ----- ");
         Serial.println(message.c_str());
      }
   }

   ///
   /// <summary>
   /// Sends the initial JSON handshake message identifying this device to the DeviceServer.
   /// </summary>
   ///
   static void _sendHandshake()
   {
      JsonDocument doc;
      doc["role"] = "device";
      doc["token"] = DEVICE_SERVER_TOKEN;
      doc["deviceId"] = std::string(WiFi.macAddress().c_str());
      doc["sketch"] = _sketchName;
      doc["version"] = _version;
      doc["buildTime"] = BUILD_TIME + 6;
      doc["site"] = _site;
      doc["location"] = _location;
      doc["board"] = ARDUINO_BOARD_VARIANT_ID;
      doc["ssid"] = std::string(WiFi.SSID().c_str());
      doc["ip"] = std::string(WiFi.localIP().toString().c_str());

      if (!_state.empty())
      {
         doc["state"] = _state;
      }

      std::string sent;
      serializeJson(doc, sent);
      _debugPrint("Sent: " + sent);

      _connection.sendJson(doc);
   }

   ///
   /// <summary>
   /// Sends any messages that were logged before the connection finished coming up.
   /// </summary>
   ///
   static void _flushPendingMessages()
   {
      for (const _PendingMessage& message : _pendingMessages)
      {
         // The queued json is a serialized object ending in '}'; splice in the message age.
         std::string json = message.json;
         json.pop_back();
         json += ",\"ageMs\":" + std::to_string(millis() - message.occurredMs) + "}";
         _connection.sendRaw(json);
      }

      _pendingMessages.clear();
   }

   ///
   /// <summary>
   /// Handles a command received from the DeviceServer. Built-in commands are answered
   /// directly; anything else is forwarded to the sketch-supplied handler registered via
   /// onCommand(), if any.
   /// </summary>
   /// <param name="command">Command text received from the DeviceServer.</param>
   ///
   static void _handleCommand(const char* command)
   {
      std::vector<std::string> args;
      std::string verb = _parseCommand(command, args);

      if (strcasecmp(verb.c_str(), "Restart") == 0)
      {
         respond("Restarting");
         reportRestarting();
         ESP.restart();
      }
      else if (strcasecmp(verb.c_str(), "GetHealth") == 0)
      {
         JsonDocument doc;
         doc["type"] = "health";
         DeviceHealth::fill(&doc);

         if (_inShellCommand)
         {
            std::string text;
            serializeJson(doc, text);
            _sendShellOutput(text.c_str());
         }
         else if (_connection.isConnected())
         {
            _connection.sendJson(doc);
         }
      }
      else if (strcasecmp(verb.c_str(), "Locate") == 0)
      {
         respond("Locating");
         if (_locateHandler != nullptr)
         {
            _locateHandler();
         }
      }
      else if (strcasecmp(verb.c_str(), "Help") == 0)
      {
         bool listOnly = false;
         bool builtinOnly = false;
         bool sketchOnly = false;
         for (const std::string& arg : args)
         {
            if (strcasecmp(arg.c_str(), "-l") == 0)
            {
               listOnly = true;
            }
            else if (strcasecmp(arg.c_str(), "-b") == 0)
            {
               builtinOnly = true;
            }
            else if (strcasecmp(arg.c_str(), "-s") == 0)
            {
               sketchOnly = true;
            }
         }
         _respondHelp(listOnly, builtinOnly, sketchOnly);
      }
      else if (strcasecmp(verb.c_str(), "I2CScan") == 0)
      {
         respond(_i2cScan().c_str());
      }
      else if (strcasecmp(verb.c_str(), "I2CRead") == 0)
      {
         respond(_i2cRead(args).c_str());
      }
      else if (strcasecmp(verb.c_str(), "I2CWrite") == 0)
      {
         respond(_i2cWrite(args).c_str());
      }
      else if (strcasecmp(verb.c_str(), "ReadPin") == 0)
      {
         respond(_readPin(args).c_str());
      }
      else if (strcasecmp(verb.c_str(), "WritePin") == 0)
      {
         respond(_writePin(args).c_str());
      }
      else if (strcasecmp(verb.c_str(), "GetPinMode") == 0)
      {
         respond(_getPinMode(args).c_str());
      }
      else if (strcasecmp(verb.c_str(), "SetPinMode") == 0)
      {
         respond(_setPinMode(args).c_str());
      }
      else if (strcasecmp(verb.c_str(), "AnalogRead") == 0)
      {
         respond(_analogRead(args).c_str());
      }
      else if (strcasecmp(verb.c_str(), "GetUptime") == 0)
      {
         respond(_getUptime().c_str());
      }
      else if (strcasecmp(verb.c_str(), "GetInfo") == 0)
      {
         respond(_getInfo().c_str());
      }
      else if (strcasecmp(verb.c_str(), "GetWifiInfo") == 0)
      {
         respond(_getWifiInfo().c_str());
      }
      else if (strcasecmp(verb.c_str(), "GetResetReason") == 0)
      {
         respond(_getResetReason().c_str());
      }
      else if (strcasecmp(verb.c_str(), "GetMemory") == 0)
      {
         respond(_getMemory().c_str());
      }
      else if (strcasecmp(verb.c_str(), "GetCpuInfo") == 0)
      {
         respond(_getCpuInfo().c_str());
      }
      else if (strcasecmp(verb.c_str(), "GetVersion") == 0)
      {
         respond(_getVersion().c_str());
      }
      else if (_commandHandler != nullptr)
      {
         _commandHandler(command);
      }
      else
      {
         log(std::string("Unknown command: ") + command);
      }
   }

   ///
   /// <summary>
   /// Handles a line of raw text received from the DeviceServer over the separate shell
   /// channel (see onShellLine()/shell()). By default, the line is dispatched through the
   /// same built-in/sketch-registered command table as the "command"/"response" channel
   /// (see _handleCommand()), so every command (Help, GetHealth, ReadPin, sketch-specific
   /// commands registered via onCommand(), etc.) also works when typed into the shell;
   /// any respond() call made while handling it is sent as "shellOutput" instead of
   /// "response". The line is also appended to the shell stream (for code reading via
   /// shell() as a Stream) and forwarded to the sketch-registered line handler, if any,
   /// for additional/custom raw handling. Empty or whitespace-only lines are ignored
   /// entirely (not dispatched as a command, not appended to the shell stream) unless a
   /// prompt is pending (see promptShellLine()), since a blank answer is meaningful there
   /// (e.g. accepting a default value) and must still reach the callback.
   /// </summary>
   /// <param name="line">Line of text received from the DeviceServer's shell channel.</param>
   ///
   static void _handleShellInput(const char* line)
   {
      bool isBlank = true;
      for (const char* p = line; *p != '\0'; p++)
      {
         if (!isspace((unsigned char)*p))
         {
            isBlank = false;
            break;
         }
      }
      if (isBlank && _pendingPromptCallback == nullptr)
      {
         return;
      }

      _shell._onInput(line);

      if (_pendingPromptCallback != nullptr)
      {
         void (*callback)(const char* line) = _pendingPromptCallback;
         _pendingPromptCallback = nullptr;

         _inShellCommand = true;
         callback(line);
         _inShellCommand = false;
         return;
      }

      _inShellCommand = true;
      _handleCommand(line);
      _inShellCommand = false;

      if (_shellHandler != nullptr)
      {
         _shellHandler(line);
      }
   }

public:
   ///
   /// <summary>
   /// Enables (or disables) detailed Serial tracing of DeviceServer communication:
   /// connects/disconnects, low-level errors, and every message sent/received/queued,
   /// each printed as "DeviceServer ----- ...". Intended for diagnosing connection issues
   /// (e.g. a handshake that's accepted locally but not acknowledged by the server);
   /// call before or after begin(), since it only affects Serial output. Off by default.
   /// </summary>
   /// <param name="enabled">True to enable debug tracing; false to disable it.</param>
   ///
   static void enableDebug(bool enabled = true)
   {
      _debugEnabled = enabled;
   }

   ///
   /// <summary>
   /// Connects to the DeviceServer (local Raspberry server first, falling back to the
   /// public production server if the primary isn't reachable within a few seconds) and
   /// starts logging. The connection completes asynchronously; the "Hub... " label
   /// printed here is completed later once the connection succeeds or fails. Call once
   /// from setup(), as the last init step (after WiFi, Influx, sensors, etc.), so no
   /// other log lines can interleave with the pending completion.
   /// </summary>
   /// <param name="sketchName">Sketch name, sent in the handshake and used for identification on the server.</param>
   /// <param name="version">Sketch version, sent in the handshake (may be nullptr if not tracked).</param>
   /// <param name="site">Resolved site name, sent in the handshake (may be nullptr if not used).</param>
   /// <param name="location">Resolved location name, sent in the handshake (may be nullptr if not used).</param>
   ///
   static void begin(const char* sketchName, const char* version, const char* site = nullptr, const char* location = nullptr)
   {
      _sketchName = sketchName != nullptr ? sketchName : "";
      _version = version != nullptr ? version : "";
      _site = site != nullptr ? site : "";
      _location = location != nullptr ? location : "";

      _connection.setToken(DEVICE_SERVER_TOKEN);
      _connection.setFallbackEndpoint(DEVICE_SERVER_SERVER_PRODUCTION_HOST, DEVICE_SERVER_SERVER_PRODUCTION_PORT, DEVICE_SERVER_SERVER_PRODUCTION_USE_TLS);

      DeviceHealth::begin();

      Util::onResetting = []() { reportRestarting(); };
      OTAUpdater::onState = [](const char* state) { setState(state); };

      logPartial("Hub... ");

      _shell.begin(_sendShellOutput);

      _connection.begin(DEVICE_SERVER_SERVER_RASPBERRY_HOST, DEVICE_SERVER_SERVER_RASPBERRY_PORT, DEVICE_SERVER_SERVER_RASPBERRY_USE_TLS, DEVICE_SERVER_PATH,
         HEARTBEAT_PING_MS, HEARTBEAT_TIMEOUT_MS, HEARTBEAT_FAILURES);
   }

   ///
   /// <summary>
   /// Drives the WebSocket connection; call once per loop() iteration.
   /// </summary>
   ///
   static void loop()
   {
      _connection.loop();

      if (_outageActive && (millis() - _outageStartMs) >= OUTAGE_RESET_M * 60UL * 1000UL)
      {
         _outageActive = false;
         Util::reset(OUTAGE_RESET_DELAY_S, "Hub unreachable");
      }

      if (_disconnectPending && !_connection.isConnected() && (millis() - _disconnectStartMs) >= DISCONNECT_GRACE_MS)
      {
         _backdateMs = millis() - _offlineSinceMs;
         log(std::string("Logging disconnected: ") + _pendingDisconnectReason);
         _backdateMs = 0;
         _disconnectPending = false;
         _disconnectLogged = true;
      }
   }

   ///
   /// <summary>
   /// Returns whether the WebSocket connection to the DeviceServer is currently up and has
   /// been acknowledged.
   /// </summary>
   /// <returns>True if connected and acknowledged; otherwise false.</returns>
   ///
   static bool isConnected()
   {
      return _connection.isConnected() && _connection.isReady();
   }

   ///
   /// <summary>
   /// Indicates whether the client is currently connected to the primary (local
   /// Raspberry) endpoint rather than the public/production fallback endpoint. See
   /// WebSocketJsonClient::isDirectConnection().
   /// </summary>
   /// <returns>True if connected to the primary (local) endpoint; false if using the fallback.</returns>
   ///
   static bool isDirectConnection()
   {
      return _connection.isDirectConnection();
   }

   ///
   /// <summary>
   /// Gets the host name of the endpoint currently being used (no scheme, port or path).
   /// </summary>
   /// <returns>The server host name.</returns>
   ///
   static std::string getHost()
   {
      return _connection.getHost();
   }

   ///
   /// <summary>
   /// Returns whether begin()'s initial connection attempt has resolved (its
   /// "Hub... " label has been completed by _onEvent(), either with "Direct"/"OK"
   /// on success or "FAILED" on failure). Used by ArduinoBase::waitForClient() to
   /// block until that label is complete, the same way a telemetry client's
   /// isStarted() is used.
   /// </summary>
   /// <returns>True once the initial connection attempt has succeeded or failed.</returns>
   ///
   static bool isResolved()
   {
      return !_linePending;
   }

   ///
   /// <summary>
   /// Records the reason reported for the device's previous shutdown (e.g. "Firmware
   /// Update"), so GetResetReason/GetInfo report it in place of the generic hardware
   /// reset reason. Called by SerialX::begin() with the value recorded via
   /// Util::setHaltReason() on the previous boot (see SerialX::lastShutdownReason()).
   /// </summary>
   /// <param name="reason">The recorded shutdown reason, or empty if none was recorded.</param>
   ///
   static void setLastShutdownReason(const char* reason)
   {
      _lastShutdownReason = reason;
   }

   ///
   /// <summary>
   /// Registers a handler invoked for commands received from the DeviceServer that aren't
   /// one of the built-in commands. The handler should call respond() to
   /// send a reply, if any. Only one handler is supported; call once from setup(), after
   /// begin().
   /// </summary>
   /// <param name="handler">Function invoked with the received command text.</param>
   /// <param name="help">Optional array describing the sketch's own commands (usage and description), appended to the "Help" command's listing; must outlive the sketch.</param>
   /// <param name="helpCount">Number of entries in help.</param>
   ///
   static void onCommand(void (*handler)(const char* command), const CommandInfo* help = nullptr, size_t helpCount = 0)
   {
      _commandHandler = handler;
      _sketchCommands = help;
      _sketchCommandCount = helpCount;
   }

   ///
   /// <summary>
   /// Registers a handler invoked for each line of raw text received from the
   /// DeviceServer's separate shell channel (see shell()). Unlike onCommand(), this is
   /// never dispatched through the built-in command table/Help listing; it's a
   /// completely independent, unparsed text channel the sketch can use however it
   /// likes (e.g. its own mini-REPL). Only one handler is supported; call once from
   /// setup(), after begin().
   /// </summary>
   /// <param name="handler">Function invoked with each line of text received from the shell channel.</param>
   ///
   static void onShellLine(void (*handler)(const char* line))
   {
      _shellHandler = handler;
   }

   ///
   /// <summary>
   /// Returns the Stream-like object backed by the DeviceServer's shell channel: text
   /// written to it (print()/println()/etc.) is sent out as "shellOutput" messages, and
   /// "shellInput" text received from the DeviceServer is queued for available()/read()/
   /// peek(). Lets existing Serial-based prompt/shell code run unmodified against the
   /// WebSocket connection instead, by writing it against Stream/Print.
   /// </summary>
   /// <returns>The shell stream.</returns>
   ///
   static WebSocketStream& shell()
   {
      return _shell;
   }

   ///
   /// <summary>
   /// Requests that the next line of text received over the shell channel be delivered to
   /// callback instead of being dispatched as a command. Lets a command handler mirror a
   /// blocking Serial-style prompt (e.g. SerialX::prompt()) without actually blocking: the
   /// handler sends its prompt text (e.g. via shell()), calls promptShellLine() with a
   /// callback to resume with the answer, then returns immediately. The callback runs as
   /// if it were handling a shell command (respond() routes to "shellOutput"), so it can
   /// chain further prompts by calling promptShellLine() again, or call respond() to finish.
   /// Only one pending prompt is supported at a time; a new call replaces any previous one.
   /// </summary>
   /// <param name="callback">Function invoked with the next shell input line as the prompt answer.</param>
   ///
   static void promptShellLine(void (*callback)(const char* line))
   {
      _pendingPromptCallback = callback;
   }

   ///
   /// <summary>
   /// Returns true while a command received over the shell channel is being handled (see
   /// _handleShellInput()/respond()). Lets a command handler decide whether it's safe to
   /// start an interactive prompt via promptShellLine(), which only makes sense for shell
   /// input (there's no equivalent for the formal command/response channel).
   /// </summary>
   /// <returns>True if the command currently being handled arrived over the shell channel.</returns>
   ///
   static bool isShellCommand()
   {
      return _inShellCommand;
   }

   ///
   /// <summary>
   /// Registers the handler invoked by the built-in "Locate" command, which asks the board
   /// to do something visibly unique so it can be identified.
   /// </summary>
   /// <param name="handler">Function invoked when a Locate command is received.</param>
   ///
   static void onLocate(void (*handler)())
   {
      _locateHandler = handler;
   }

   ///
   /// <summary>
   /// Sends a reply to the DeviceServer in response to a received command. Does nothing if
   /// not currently connected, since a command can only have been received while connected.
   /// If the command currently being handled arrived over the shell channel (see
   /// _handleShellInput()), the reply is sent as "shellOutput" text (with a trailing
   /// newline, so the DeviceServer starts a fresh ">" prompt) instead of a formal
   /// "response" message, so the two channels never get mixed.
   /// </summary>
   /// <param name="message">Reply text to send.</param>
   ///
   static void respond(const char* message)
   {
      if (_inShellCommand)
      {
         // Finalized command replies (unlike interactive prompts) always end with a
         // newline, so the DeviceServer shows a fresh ">" prompt instead of leaving the
         // cursor sitting at the end of the response.
         _sendShellOutput((std::string(message) + "\n").c_str());
         return;
      }

      if (_connection.isConnected())
      {
         JsonDocument doc;
         doc["type"] = "response";
         doc["message"] = message;

         _connection.sendJson(doc);
      }
   }

   ///
   /// <summary>
   /// Sends a line of text to the DeviceServer over the separate shell channel (see
   /// shell()/onShellLine()). Does nothing if not currently connected. Passed as the
   /// send callback to the shell stream's begin(); not normally called directly, write
   /// to shell() instead.
   /// </summary>
   /// <param name="text">Line of shell output text to send (no trailing newline).</param>
   ///
   static void _sendShellOutput(const char* text)
   {
      if (_connection.isConnected())
      {
         JsonDocument doc;
         doc["type"] = "shellOutput";
         doc["text"] = text;

         _connection.sendJson(doc);
      }
   }

   ///
   /// <summary>
   /// Reports the RESTARTING state and pauses briefly so the message is transmitted
   /// before the device restarts. Call immediately before restarting.
   /// </summary>
   ///
   static void reportRestarting()
   {
      setState("RESTARTING");
      delay(RESTART_FLUSH_MS);
   }

   ///
   /// <summary>
   /// Records the device's current state (e.g. "RUNNING") and reports it to the DeviceServer
   /// as {"type":"state","state":...}. The latest state is also included in the handshake,
   /// so a state set before the connection comes up is delivered once it does. Does nothing
   /// if the state hasn't changed.
   /// </summary>
   /// <param name="state">State label to report.</param>
   ///
   static void setState(const char* state)
   {
      if (_state == state)
      {
         return;
      }

      _state = state;

      if (_connection.isConnected())
      {
         JsonDocument doc;
         doc["type"] = "state";
         doc["state"] = _state;

         _connection.sendJson(doc);
      }
   }

   ///
   /// <summary>
   /// Sets the single default tag/component sent with subsequent log() / logPartial()
   /// calls that don't specify their own tags (e.g. "WiFi", "Sensors"). Useful for
   /// scoping a block of related log calls without repeating the tag on each one.
   /// Replaces any tags set by a previous setTag()/setTags() call.
   /// </summary>
   /// <param name="tag">Tag text to use as the default from now on.</param>
   ///
   static void setTag(const char* tag)
   {
      _tags = { tag };
   }

   ///
   /// <summary>
   /// Sets the default tags/components sent with subsequent log() / logPartial() calls
   /// that don't specify their own tags. Replaces any tags set by a previous
   /// setTag()/setTags() call.
   /// </summary>
   /// <param name="tags">Tags to use as the default from now on.</param>
   ///
   static void setTags(const std::vector<std::string>& tags)
   {
      _tags = tags;
   }

   ///
   /// <summary>
   /// Sends a log message to the DeviceServer if connected, or queues it to be sent once
   /// the connection completes (e.g. messages logged during setup(), before the
   /// WebSocket has had a chance to finish connecting). Completes the current log entry
   /// (sent with "final":true), so call this last, after any logPartial() calls
   /// building up the same line. Always echoes to Serial.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message, sent to the server as its level.</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags() (or {"Initializing"} until logInitializationComplete()).</param>
   ///
   static void log(const char* message, LogSeverity severity = LogSeverity::INFO, const std::vector<std::string>& tags = {})
   {
      _sendOrQueueLog(message, severity, !tags.empty() ? tags : _tags, true);

      if (!_linePending)
      {
         Serial.print("Logger ----- ");
      }
      Serial.println(message);

      _linePending = false;
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity, const std::vector<std::string>&) for callers passing a single tag.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message, sent to the server as its level.</param>
   /// <param name="tag">Tag/component the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void log(const char* message, LogSeverity severity, const char* tag)
   {
      log(message, severity, std::vector<std::string>{ tag });
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity, const std::vector<std::string>&) for callers holding a std::string.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message, sent to the server as its level.</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void log(const std::string& message, LogSeverity severity = LogSeverity::INFO, const std::vector<std::string>& tags = {})
   {
      log(message.c_str(), severity, tags);
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity, const char*) for callers holding a std::string and a single tag.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message, sent to the server as its level.</param>
   /// <param name="tag">Tag/component the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void log(const std::string& message, LogSeverity severity, const char* tag)
   {
      log(message.c_str(), severity, tag);
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity, const std::vector<std::string>&) for callers holding an Arduino String.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message, sent to the server as its level.</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void log(const String& message, LogSeverity severity = LogSeverity::INFO, const std::vector<std::string>& tags = {})
   {
      log(message.c_str(), severity, tags);
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity, const char*) for callers holding an Arduino String and a single tag.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message, sent to the server as its level.</param>
   /// <param name="tag">Tag/component the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void log(const String& message, LogSeverity severity, const char* tag)
   {
      log(message.c_str(), severity, tag);
   }

   ///
   /// <summary>
   /// Sends a message fragment to the DeviceServer without completing the log entry,
   /// allowing the result to be appended later via a subsequent logPartial()/log() call
   /// (e.g. printing "WiFi... " now and "OK" once the connection result is known). The
   /// DeviceServer joins fragments into a single entry until one is sent with "final":true
   /// (see log()). Echoes to Serial the same way, without a trailing newline. Pass the
   /// severity on the completing log() call instead, since that's what determines the level.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void logPartial(const char* message, const std::vector<std::string>& tags = {})
   {
      _sendOrQueueLog(message, LogSeverity::INFO, !tags.empty() ? tags : _tags, false);

      if (!_linePending)
      {
         Serial.print("Logger ----- ");
      }
      Serial.print(message);

      _linePending = true;
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*, const std::vector<std::string>&) for callers passing a single tag.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   /// <param name="tag">Tag/component the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void logPartial(const char* message, const char* tag)
   {
      logPartial(message, std::vector<std::string>{ tag });
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*, const std::vector<std::string>&) for callers holding a std::string.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void logPartial(const std::string& message, const std::vector<std::string>& tags = {})
   {
      logPartial(message.c_str(), tags);
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*, const char*) for callers holding a std::string and a single tag.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   /// <param name="tag">Tag/component the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void logPartial(const std::string& message, const char* tag)
   {
      logPartial(message.c_str(), tag);
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*, const std::vector<std::string>&) for callers holding an Arduino String.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void logPartial(const String& message, const std::vector<std::string>& tags = {})
   {
      logPartial(message.c_str(), tags);
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*, const char*) for callers holding an Arduino String and a single tag.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   /// <param name="tag">Tag/component the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void logPartial(const String& message, const char* tag)
   {
      logPartial(message.c_str(), tag);
   }

   ///
   /// <summary>
   /// Logs the standard "initialization complete" message shared by every sketch (see
   /// SketchBase::begin() and ViewerSketch::begin()), so the wording can't drift between
   /// sketches. Also clears the default {"Initializing"} tag set at startup (see setTag()),
   /// since subsequent log calls no longer belong to the init sequence. Call once, at the
   /// very end of the sketch's boot/init sequence.
   /// </summary>
   ///
   static void logInitializationComplete()
   {
      log("Initialization complete. Sketch running.");
      setTags({});
   }
};
