#pragma once

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

#include "ArduinoBoardId.h"
#include "DeviceHealth.h"
#include "Format.h"
#include "OTAUpdater.h"
#include "Timer.h"
#include "WebSocketJsonClient.h"
#include "WiFiSettings.h"

///
/// <summary>
/// Severity of a log message sent to the Device Server. WARN and ERROR messages are
/// prefixed with "WARN: "/"ERROR: " by DeviceServerClient itself, so callers don't need to
/// embed the prefix in their message text.
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
/// Collects name/value pairs for a GetStatus reply (see DeviceServerClient::onStatus()).
/// The base set of fields (sketch/version/site/location/etc.) is populated by
/// DeviceServerClient itself; a sketch-registered handler can add its own fields (e.g. a
/// wind sensor adding the current wind speed) before the reply is sent.
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
/// WebSocket client for the DeviceServer (a separate server/process from TelemetryClient;
/// see TelemetryClient.h), used to send log messages and respond to device-management
/// commands. Unlike the old LogServer protocol (plain text frames), every message
/// exchanged with the DeviceServer is a JSON object.
/// </summary>
/// <remarks>
/// Protocol (JSON text frames), reusing the DeviceServer's device role/token:
///   Handshake, sent on connect:  {"role":"device","token":"...","deviceId":"...","sketch":"...","version":"...","site":"...","location":"..."}
///   Server reply:                {"type":"ack"}
///   Log entry (device to hub):   {"type":"log","level":"Info"|"Warn"|"Error"|"Debug","tags":"wifi,reconnect","message":"...","final":true}
///   Command (hub to device):     {"type":"command","command":"..."}
///   Command response:            {"type":"response","message":"..."}
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

         if (!DeviceServerClient::_everConnected)
         {
            if (!DeviceServerClient::_initialFailureLogged)
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
      }

      void _onError(const std::string& reason) override
      {
         DeviceServerClient::_debugPrint("Error: " + reason);
         DeviceServerClient::log("Error: " + reason);
      }
   };

   /// <summary>How long (in milliseconds) DeviceServerClient waits after a disconnect before logging "Logging disconnected: ...".</summary>
   static constexpr uint32_t DISCONNECT_GRACE_MS = 3000UL;

   static inline _Connection _connection;
   static inline bool _everConnected = false;
   static inline bool _initialFailureLogged = false;
   static inline std::string _sketchName;
   static inline std::string _version;
   static inline std::string _site;
   static inline std::string _location;

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
   /// <summary>Optional sketch-supplied handler that adds fields to a GetStatus reply (see onStatus()).</summary>
   static inline void (*_statusHandler)(LoggerStatus& status) = nullptr;

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
   /// Returns "N/A" if the given string is empty; otherwise returns it unchanged. Used
   /// for GetStatus fields (e.g. Site/Location) that aren't set by every sketch.
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
   /// Handles a command received from the DeviceServer. The built-in "GetStatus" command
   /// is answered directly, populated with the base status fields plus any added by the
   /// sketch-registered handler (see onStatus()); anything else is forwarded to the
   /// sketch-supplied handler registered via onCommand(), if any.
   /// </summary>
   /// <param name="command">Command text received from the DeviceServer.</param>
   ///
   static void _handleCommand(const char* command)
   {
      if (strcasecmp(command, "Restart") == 0)
      {
         respond("Restarting");
         reportRestarting();
         ESP.restart();
      }
      else if (strcasecmp(command, "GetStatus") == 0)
      {
         LoggerStatus status;
         status.add("Sketch", _sketchName);
         status.add("Version", _version);
         status.add("Device ID", std::string(WiFi.macAddress().c_str()));
         status.add("Site", _orNA(_site));
         status.add("Location", _orNA(_location));
         status.add("WiFi SSID", std::string(WiFi.SSID().c_str()));
         status.add("IP Address", std::string(WiFi.localIP().toString().c_str()));
         status.add("Signal Strength", std::to_string(WiFi.RSSI()) + " dBm");
         status.add("Uptime", std::string(formatDuration(millis()).c_str()));
         status.add("Free Heap", std::string(formatBytes(ESP.getFreeHeap()).c_str()));

         if (_statusHandler != nullptr)
         {
            _statusHandler(status);
         }

         log(status.toString());
      }
      else if (strcasecmp(command, "GetHealth") == 0)
      {
         if (_connection.isConnected())
         {
            JsonDocument doc;
            doc["type"] = "health";
            DeviceHealth::fill(&doc);

            _connection.sendJson(doc);
         }
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
   /// Registers a handler invoked for commands received from the DeviceServer that aren't
   /// one of the built-in commands ("GetStatus"). The handler should call respond() to
   /// send a reply, if any. Only one handler is supported; call once from setup(), after
   /// begin().
   /// </summary>
   /// <param name="handler">Function invoked with the received command text.</param>
   ///
   static void onCommand(void (*handler)(const char* command))
   {
      _commandHandler = handler;
   }

   ///
   /// <summary>
   /// Registers a handler invoked when a "GetStatus" command is received, after the base
   /// status fields (sketch/version/site/location/etc.) have been added, allowing a
   /// sketch to append its own fields (e.g. a wind sensor adding the current wind
   /// speed). Only one handler is supported; call once from setup(), after begin().
   /// </summary>
   /// <param name="handler">Function invoked with the in-progress status to add fields to.</param>
   ///
   static void onStatus(void (*handler)(LoggerStatus& status))
   {
      _statusHandler = handler;
   }

   ///
   /// <summary>
   /// Sends a reply to the DeviceServer in response to a received command. Does nothing if
   /// not currently connected, since a command can only have been received while connected.
   /// </summary>
   /// <param name="message">Reply text to send.</param>
   ///
   static void respond(const char* message)
   {
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
   /// <param name="severity">Severity of the message; WARN/ERROR are prefixed with "WARN: "/"ERROR: ".</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags() (or {"Initializing"} until logInitializationComplete()).</param>
   ///
   static void log(const char* message, LogSeverity severity = LogSeverity::INFO, const std::vector<std::string>& tags = {})
   {
      std::string prefix = severity == LogSeverity::ERROR ? "ERROR: " : severity == LogSeverity::WARN ? "WARN: " : "";
      std::string text = prefix + message;

      _sendOrQueueLog(text, severity, !tags.empty() ? tags : _tags, true);

      if (!_linePending)
      {
         Serial.print("Logger ----- ");
      }
      Serial.println(text.c_str());

      _linePending = false;
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity, const std::vector<std::string>&) for callers passing a single tag.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message; WARN/ERROR are prefixed with "WARN: "/"ERROR: ".</param>
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
   /// <param name="severity">Severity of the message; WARN/ERROR are prefixed with "WARN: "/"ERROR: ".</param>
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
   /// <param name="severity">Severity of the message; WARN/ERROR are prefixed with "WARN: "/"ERROR: ".</param>
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
   /// <param name="severity">Severity of the message; WARN/ERROR are prefixed with "WARN: "/"ERROR: ".</param>
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
   /// <param name="severity">Severity of the message; WARN/ERROR are prefixed with "WARN: "/"ERROR: ".</param>
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
   /// (see log()). Echoes to Serial the same way, without a trailing newline. A partial
   /// fragment is never itself prefixed with "WARN: "/"ERROR: " -- pass the severity on
   /// the completing log() call instead, since that's what determines the prefix.
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
