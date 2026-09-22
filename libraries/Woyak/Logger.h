#pragma once

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <WebSocketsClient.h>

#include "WiFiSettings.h"

///
/// <summary>
/// How often (in milliseconds) LoggerClass proactively pings the LogServer, via
/// WebSocketsClient::enableHeartbeat(). Keeps idle Logger connections detected/alive
/// independent of the server's own ping cadence.
/// </summary>
///
constexpr uint32_t LOG_HEARTBEAT_PING_INTERVAL_MS = 30000UL;

///
/// <summary>
/// How long (in milliseconds) LoggerClass waits for a pong reply to its heartbeat
/// ping before counting it as missed.
/// </summary>
///
constexpr uint32_t LOG_HEARTBEAT_PONG_TIMEOUT_MS = 10000UL;

///
/// <summary>
/// Number of consecutive missed heartbeat pongs before WebSocketsClient tears down
/// and reconnects the LogServer connection.
/// </summary>
///
constexpr uint8_t LOG_HEARTBEAT_DISCONNECT_COUNT = 2;

///
/// <summary>
/// Severity of a log message passed to LoggerClass::log()/logPartial(). ERROR messages
/// are prefixed with "ERROR: " by the Logger itself, so callers don't need to embed the
/// prefix in their message text. Kept as a plain text prefix for now; may be replaced
/// with structured metadata (e.g. JSON) in the future.
/// </summary>
///
enum class LogSeverity
{
   INFO,
   ERROR
};

///
/// <summary>
/// Collects name/value pairs for a GetStatus reply (see LoggerClass::onStatus()). The
/// base set of fields (sketch/version/site/location/etc.) is populated by
/// LoggerClass itself; a sketch-registered handler can add its own fields (e.g. a wind
/// sensor adding the current wind speed) before the reply is sent.
/// </summary>
///
class LoggerStatus
{
   std::vector<std::string> _lines;

public:
   ///
   /// <summary>
   /// Adds a name/value pair to the status reply.
   /// </summary>
   /// <param name="name">Field name (e.g. "Wind Speed").</param>
   /// <param name="value">Field value, already formatted as desired (e.g. "12.3 mph").</param>
   ///
   void add(const char* name, const std::string& value)
   {
      _lines.push_back(std::string(name) + ": " + value);
   }

   ///
   /// <summary>
   /// Overload of add(const char*, const std::string&) for callers holding a C string.
   /// </summary>
   /// <param name="name">Field name.</param>
   /// <param name="value">Field value.</param>
   ///
   void add(const char* name, const char* value)
   {
      add(name, std::string(value));
   }

   ///
   /// <summary>
   /// Overload of add(const char*, const std::string&) for callers holding an int.
   /// </summary>
   /// <param name="name">Field name.</param>
   /// <param name="value">Field value.</param>
   ///
   void add(const char* name, int value)
   {
      add(name, std::to_string(value));
   }

   ///
   /// <summary>
   /// Overload of add(const char*, const std::string&) for callers holding a uint32_t.
   /// </summary>
   /// <param name="name">Field name.</param>
   /// <param name="value">Field value.</param>
   ///
   void add(const char* name, uint32_t value)
   {
      add(name, std::to_string(value));
   }

   ///
   /// <summary>
   /// Overload of add(const char*, const std::string&) for callers holding a float,
   /// formatted to a fixed number of decimal places.
   /// </summary>
   /// <param name="name">Field name.</param>
   /// <param name="value">Field value.</param>
   /// <param name="decimals">Number of decimal places to format with.</param>
   ///
   void add(const char* name, float value, uint8_t decimals = 1)
   {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision(decimals) << value;
      add(name, stream.str());
   }

   ///
   /// <summary>
   /// Overload of add(const char*, float, uint8_t) for callers holding a std::string
   /// name (e.g. one built up via concatenation) rather than a const char*.
   /// </summary>
   /// <param name="name">Field name.</param>
   /// <param name="value">Field value.</param>
   /// <param name="decimals">Number of decimal places to format with.</param>
   ///
   void add(const std::string& name, float value, uint8_t decimals = 1)
   {
      add(name.c_str(), value, decimals);
   }

   ///
   /// <summary>
   /// Joins all added fields into the final reply text, one "Name: value" pair per line.
   /// </summary>
   /// <returns>The reply text.</returns>
   ///
   std::string toString() const
   {
      std::string result;
      for (size_t i = 0; i < _lines.size(); i++)
      {
         if (i > 0)
         {
            result += "\n";
         }
         result += _lines[i];
      }
      return result;
   }
};

///
/// <summary>
/// WebSocket client for the LogServer (C:\SourceCode\LogServer), used to send free-form
/// text log messages from a sketch. On connect, sends a JSON handshake identifying the
/// device (deviceId/sketch/version), then each log() call sends a single text message.
/// Connection is best-effort: a dropped/failed connection is logged to Serial but does
/// not reset the device, since text logging shouldn't be able to crash a sketch that
/// otherwise works fine. All members are static since a sketch has a single LogServer
/// connection; use the global Logger instance below (mirroring Serial), e.g. Logger.log(...).
/// </summary>
///
class LoggerClass
{
   static inline WebSocketsClient _webSocket;
   static inline bool _connected = false;
   static inline bool _everConnected = false;
   static inline std::string _sketchName;
   static inline std::string _version;
   static inline std::string _site;
   static inline std::string _location;
   static inline std::string _sensor;

   /// <summary>Messages logged before the connection was up, sent once it completes.</summary>
   static inline std::vector<std::string> _pendingMessages;

   /// <summary>True if a logPartial() call is still awaiting its completing log() call.</summary>
   static inline bool _linePending = false;

   /// <summary>Optional sketch-supplied handler for commands not recognized as built-in (see onCommand()).</summary>
   static inline void (*_commandHandler)(const char* command) = nullptr;
   /// <summary>Optional sketch-supplied handler that adds fields to a GetStatus reply (see onStatus()).</summary>
   static inline void (*_statusHandler)(LoggerStatus& status) = nullptr;
   /// <summary>Optional handler invoked when a "ForceOTA" command is received (see onForceOTA()).</summary>
   static inline void (*_forceOTAHandler)() = nullptr;

   ///
   /// <summary>
   /// Sends a text message over the WebSocket.
   /// </summary>
   /// <param name="message">Message text to send.</param>
   ///
   static void _send(const char* message)
   {
      _webSocket.sendTXT(message);
   }

   ///
   /// <summary>
   /// Sends a message fragment to the LogServer if connected, or queues it to be sent
   /// once the connection completes. Used by logPartial()/log() to send message
   /// fragments that the LogServer joins into a single entry until a fragment without
   /// endLine set arrives.
   /// </summary>
   /// <param name="message">Message text to send or queue.</param>
   ///
   static void _sendOrQueue(const char* message)
   {
      if (_connected)
      {
         _send(message);
      }
      else
      {
         _pendingMessages.push_back(message);
      }
   }

   ///
   /// <summary>
   /// Sends the initial JSON handshake message identifying this device to the LogServer.
   /// </summary>
   ///
   static void _sendHandshake()
   {
      std::string deviceId = WiFi.macAddress().c_str();

      std::string message = "{\"deviceId\":\"" + deviceId +
         "\",\"sketch\":\"" + _sketchName +
         "\",\"version\":\"" + _version +
         "\",\"site\":\"" + _site +
         "\",\"location\":\"" + _location +
         "\",\"sensor\":\"" + _sensor + "\"}";

      _send(message.c_str());
   }

   ///
   /// <summary>
   /// Sends any messages that were logged before the connection finished coming up.
   /// </summary>
   ///
   static void _flushPendingMessages()
   {
      for (const std::string& message : _pendingMessages)
      {
         _send(message.c_str());
      }

      _pendingMessages.clear();
   }

   ///
   /// <summary>
   /// Returns "N/A" if the given string is empty; otherwise returns it unchanged. Used
   /// for GetStatus fields (e.g. Site/Location/Sensor) that aren't set by every sketch.
   /// </summary>
   /// <param name="value">String value to check.</param>
   /// <returns>"N/A" if value is empty; otherwise value.</returns>
   ///
   static const std::string& _orNA(const std::string& value)
   {
      static const std::string NOT_AVAILABLE = "N/A";
      return value.empty() ? NOT_AVAILABLE : value;
   }

   ///
   /// <summary>
   /// Handles a command received from the LogServer. The built-in "GetStatus" command
   /// is answered directly, populated with the base status fields plus any added by the
   /// sketch-registered handler (see onStatus()); anything else is forwarded to the
   /// sketch-supplied handler registered via onCommand(), if any.
   /// </summary>
   /// <param name="command">Command text received from the LogServer.</param>
   ///
   static void _handleCommand(const char* command)
   {
      if (strcasecmp(command, "Restart") == 0)
      {
         respond("Restarting");
         ESP.restart();
      }
      else if (strcasecmp(command, "ForceOTA") == 0)
      {
         if (_forceOTAHandler != nullptr)
         {
            respond("Forcing OTA update");
            _forceOTAHandler();
         }
         else
         {
            respond("ForceOTA not supported: OTA is not enabled");
         }
      }
      else if (strcasecmp(command, "GetStatus") == 0)
      {
         LoggerStatus status;
         status.add("Sketch", _sketchName);
         status.add("Version", _version);
         status.add("Device ID", std::string(WiFi.macAddress().c_str()));
         status.add("Site", _orNA(_site));
         status.add("Location", _orNA(_location));
         status.add("Sensor", _orNA(_sensor));
         status.add("WiFi SSID", std::string(WiFi.SSID().c_str()));
         status.add("IP Address", std::string(WiFi.localIP().toString().c_str()));
         status.add("Signal Strength", std::to_string(WiFi.RSSI()) + " dBm");
         status.add("Uptime", std::to_string(millis() / 1000UL) + " secs");
         status.add("Free Heap", std::to_string(ESP.getFreeHeap()) + " bytes");

         if (_statusHandler != nullptr)
         {
            _statusHandler(status);
         }

         respond(status.toString().c_str());
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
   /// Handles WebSocket lifecycle events: sends the handshake and any pending messages
   /// on connect, and tracks the connected state so log() knows whether to queue or send
   /// immediately.
   /// </summary>
   /// <param name="type">The event type reported by WebSocketsClient.</param>
   /// <param name="payload">The event payload, if any.</param>
   /// <param name="length">The length of the payload, in bytes.</param>
   ///
   static void _onEvent(WStype_t type, uint8_t* payload, size_t length)
   {
      switch (type)
      {
         case WStype_CONNECTED:
            _connected = true;
            _sendHandshake();
            _flushPendingMessages();
            if (!_everConnected)
            {
               _everConnected = true;
               log(LOG_SERVER_HOST);
            }
            else
            {
               log("Logging reconnected");
            }
            break;

         case WStype_DISCONNECTED:
         {
            _connected = false;
            std::string reason = (payload != nullptr && length > 0) ? std::string((const char*)payload, length) : "";
            if (!_everConnected)
            {
               log("failed");
            }
            else
            {
               log(std::string("Logging disconnected: ") + (reason.empty() ? "unknown" : reason));
            }
            break;
         }

         case WStype_ERROR:
         {
            std::string reason = (payload != nullptr && length > 0) ? std::string((const char*)payload, length) : "";
            log("Error: " + reason);
            break;
         }

         case WStype_TEXT:
         {
            std::string command = (payload != nullptr && length > 0) ? std::string((const char*)payload, length) : "";
            if (!command.empty())
            {
               _handleCommand(command.c_str());
            }
            break;
         }

         default:
            break;
      }
   }

public:
   ///
   /// <summary>
   /// Connects to the LogServer (local or remote, per the LOG_SERVER_LOCAL define in
   /// WiFiSettings.h) and registers the WebSocket event handler. The connection
   /// completes asynchronously (mirroring TelemetryClient::begin()/beginSSL()); the
   /// "Logging... " label printed here is completed later by _onEvent() once the
   /// connection succeeds or fails. Call once from setup(), as the last init step
   /// (after WiFi, Influx, sensors, etc.), so no other log lines can interleave with
   /// the pending completion.
   /// </summary>
   /// <param name="sketchName">Sketch name, sent in the handshake and used for identification on the server.</param>
   /// <param name="version">Sketch version, sent in the handshake (may be nullptr if not tracked).</param>
   /// <param name="site">Resolved site name, sent in the handshake (may be nullptr if not used).</param>
   /// <param name="location">Resolved location name, sent in the handshake (may be nullptr if not used).</param>
   /// <param name="sensor">Resolved sensor tag, sent in the handshake (may be nullptr if not used).</param>
   ///
   static void begin(const char* sketchName, const char* version, const char* site = nullptr, const char* location = nullptr, const char* sensor = nullptr)
   {
      _sketchName = sketchName != nullptr ? sketchName : "";
      _version = version != nullptr ? version : "";
      _site = site != nullptr ? site : "";
      _location = location != nullptr ? location : "";
      _sensor = sensor != nullptr ? sensor : "";

      _webSocket.onEvent(_onEvent);

      logPartial("Logging... ");

#ifdef LOG_SERVER_LOCAL
      _webSocket.begin(LOG_SERVER_HOST, LOG_SERVER_PORT, LOG_SERVER_PATH);
#else
      // The remote LogServer is only reachable over TLS (port 443); certificate
      // validation is skipped here since the WebSocketsClient library needs a pinned
      // fingerprint or CA cert to validate, which this sketch does not maintain.
      _webSocket.beginSSL(LOG_SERVER_HOST, LOG_SERVER_PORT, LOG_SERVER_PATH);
#endif

      // Proactively pings the LogServer so a dead/idle connection (e.g. a NAT/proxy
      // timeout shorter than the server's own ping interval) is detected and
      // reconnected quickly, rather than only being noticed the next time a message
      // fails to send. Unlike TelemetryClient, Logger connections can otherwise sit
      // idle for long stretches between log messages.
      _webSocket.enableHeartbeat(LOG_HEARTBEAT_PING_INTERVAL_MS, LOG_HEARTBEAT_PONG_TIMEOUT_MS, LOG_HEARTBEAT_DISCONNECT_COUNT);
   }

   ///
   /// <summary>
   /// Drives the WebSocket connection; call once per loop() iteration.
   /// </summary>
   ///
   static void loop()
   {
      _webSocket.loop();
   }

   ///
   /// <summary>
   /// Returns whether the WebSocket connection to the LogServer is currently up.
   /// </summary>
   /// <returns>True if connected; otherwise false.</returns>
   ///
   static bool isConnected()
   {
      return _connected;
   }

   ///
   /// <summary>
   /// Returns whether begin()'s initial connection attempt has resolved (its
   /// "Logging... " label has been completed by _onEvent(), either with the server
   /// host on success or "failed" on failure). Used by ArduinoBase::waitForClient() to
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
   /// Registers a handler invoked for commands received from the LogServer that aren't
   /// one of the built-in commands ("GetStatus"). The handler should call
   /// respond() to send a reply, if any. Only one handler is supported; call once from
   /// setup(), after Logger.begin().
   /// </summary>
   /// <param name="handler">Function invoked with the received command text.</param>
   ///
   static void onCommand(void (*handler)(const char* command))
   {
      _commandHandler = handler;
   }

   ///
   /// <summary>
   /// Registers a handler invoked when a "ForceOTA" command is received, which should
   /// download and install the current OTA firmware regardless of version. If no
   /// handler is registered (e.g. OTA wasn't enabled), the command replies that it
   /// isn't supported instead of doing nothing silently.
   /// </summary>
   /// <param name="handler">Function invoked to force the OTA update check/install.</param>
   ///
   static void onForceOTA(void (*handler)())
   {
      _forceOTAHandler = handler;
   }

   ///
   /// <summary>
   /// Registers a handler invoked when a "GetStatus" command is received, after the base
   /// status fields (sketch/version/site/location/etc.) have been added, allowing a
   /// sketch to append its own fields (e.g. a wind sensor adding the current wind
   /// speed). Only one handler is supported; call once from setup(), after Logger.begin().
   /// </summary>
   /// <param name="handler">Function invoked with the in-progress status to add fields to.</param>
   ///
   static void onStatus(void (*handler)(LoggerStatus& status))
   {
      _statusHandler = handler;
   }

   ///
   /// <summary>
   /// Sends a reply to the LogServer in response to a received command. Does nothing if
   /// not currently connected, since a command can only have been received while connected.
   /// </summary>
   /// <param name="message">Reply text to send.</param>
   ///
   static void respond(const char* message)
   {
      if (_connected)
      {
         _send((std::string(message) + "\n").c_str());
      }
   }

   ///
   /// <summary>
   /// Sends a text log message to the LogServer if connected, or queues it to be sent
   /// once the connection completes (e.g. messages logged during setup(), before the
   /// WebSocket has had a chance to finish connecting). Completes the current log entry
   /// (the LogServer treats a trailing newline as the end of an entry), so call this
   /// last, after any logPartial() calls building up the same line. Always echoes to
   /// Serial.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message; ERROR is prefixed with "ERROR: ".</param>
   ///
   static void log(const char* message, LogSeverity severity = LogSeverity::INFO)
   {
      std::string text = severity == LogSeverity::ERROR ? "ERROR: " + std::string(message) : message;

      _sendOrQueue((text + "\n").c_str());

      if (!_linePending)
      {
         Serial.print("Logger ----- ");
      }
      Serial.println(text.c_str());

      _linePending = false;
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity) for callers holding a std::string.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message; ERROR is prefixed with "ERROR: ".</param>
   ///
   static void log(const std::string& message, LogSeverity severity = LogSeverity::INFO)
   {
      log(message.c_str(), severity);
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity) for callers holding an Arduino String.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message; ERROR is prefixed with "ERROR: ".</param>
   ///
   static void log(const String& message, LogSeverity severity = LogSeverity::INFO)
   {
      log(message.c_str(), severity);
   }

   ///
   /// <summary>
   /// Sends a message fragment to the LogServer without completing the log entry,
   /// allowing the result to be appended later via a subsequent logPartial()/log() call
   /// (e.g. printing "WiFi... " now and "OK" once the connection result is known). The
   /// LogServer joins fragments into a single entry until one is sent with a trailing
   /// newline (see log()). Echoes to Serial the same way, without a trailing newline. A
   /// partial fragment is never itself prefixed with "ERROR: " -- pass the severity on
   /// the completing log() call instead, since that's what determines the prefix.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   ///
   static void logPartial(const char* message)
   {
      _sendOrQueue(message);

      if (!_linePending)
      {
         Serial.print("Logger ----- ");
      }
      Serial.print(message);

      _linePending = true;
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*) for callers holding a std::string.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   ///
   static void logPartial(const std::string& message)
   {
      logPartial(message.c_str());
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*) for callers holding an Arduino String.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   ///
   static void logPartial(const String& message)
   {
      logPartial(message.c_str());
   }

   ///
   /// <summary>
   /// Logs the standard "initialization complete" message shared by every sketch (see
   /// SketchBase::begin() and ViewerSketch::begin()), so the wording can't drift between
   /// sketches. Call once, at the very end of the sketch's boot/init sequence.
   /// </summary>
   ///
   static void logInitializationComplete()
   {
      log("Initialization complete. Sketch running.");
   }
};

///
/// <summary>
/// Global Logger instance shared by the whole sketch (one physical board, one LogServer
/// connection), mirroring Arduino's Serial global. Board-level helpers (see
/// ArduinoBase::printlnInitStatus(), initWifi(), initSensor(), initClient()) log through
/// this automatically, so callers only need to invoke those helpers once to update both
/// the display/Serial and the LogServer.
/// </summary>
///
inline LoggerClass Logger;
