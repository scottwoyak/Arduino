#pragma once

#include <string>
#include <vector>

#include <WebSocketsClient.h>

#include "WiFiSettings.h"

///
/// <summary>
/// WebSocket client for the LogServer (C:\SourceCode\LogServer), used to send free-form
/// text log messages from a sketch. On connect, sends a JSON handshake identifying the
/// device (deviceId/sketch/version), then each log() call sends a single text message.
/// Connection is best-effort: a dropped/failed connection is logged to Serial but does
/// not reset the device, since text logging shouldn't be able to crash a sketch that
/// otherwise works fine.
/// </summary>
///
class Logger
{
private:
   WebSocketsClient _webSocket;
   bool _connected = false;
   std::string _sketchName;
   std::string _version;
   std::string _site;
   std::string _location;
   std::string _sensor;

   /// <summary>Messages logged before the connection was up, sent once it completes.</summary>
   std::vector<std::string> _pendingMessages;

   /// <summary>True if a logPartial() call is still awaiting its completing log() call.</summary>
   bool _linePending = false;

   ///
   /// <summary>
   /// Sends a text message over the WebSocket.
   /// </summary>
   /// <param name="message">Message text to send.</param>
   ///
   void _send(const char* message)
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
   void _sendOrQueue(const char* message)
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
   void _sendHandshake()
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
   void _flushPendingMessages()
   {
      for (const std::string& message : _pendingMessages)
      {
         _send(message.c_str());
      }

      _pendingMessages.clear();
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
   void _onEvent(WStype_t type, uint8_t* payload, size_t length)
   {
      switch (type)
      {
         case WStype_CONNECTED:
            _connected = true;
            _sendHandshake();
            _flushPendingMessages();
            break;

         case WStype_DISCONNECTED:
            _connected = false;
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

public:
   ///
   /// <summary>
   /// Connects to the LogServer (local or remote, per the LOG_SERVER_LOCAL define in
   /// WiFiSettings.h) and registers the WebSocket event handler. Call once from setup(),
   /// after WiFi has connected (and, if applicable, after the site/location/sensor have
   /// been resolved).
   /// </summary>
   /// <param name="sketchName">Sketch name, sent in the handshake and used for identification on the server.</param>
   /// <param name="version">Sketch version, sent in the handshake (may be nullptr if not tracked).</param>
   /// <param name="site">Resolved site name, sent in the handshake (may be nullptr if not used).</param>
   /// <param name="location">Resolved location name, sent in the handshake (may be nullptr if not used).</param>
   /// <param name="sensor">Resolved sensor tag, sent in the handshake (may be nullptr if not used).</param>
   ///
   void begin(const char* sketchName, const char* version, const char* site = nullptr, const char* location = nullptr, const char* sensor = nullptr)
   {
      _sketchName = sketchName != nullptr ? sketchName : "";
      _version = version != nullptr ? version : "";
      _site = site != nullptr ? site : "";
      _location = location != nullptr ? location : "";
      _sensor = sensor != nullptr ? sensor : "";

      _webSocket.onEvent([this](WStype_t type, uint8_t* payload, size_t length) { _onEvent(type, payload, length); });

#ifdef LOG_SERVER_LOCAL
      _webSocket.begin(LOG_SERVER_HOST, LOG_SERVER_PORT, LOG_SERVER_PATH);
#else
      // The remote LogServer is only reachable over TLS (port 443); certificate
      // validation is skipped here since the WebSocketsClient library needs a pinned
      // fingerprint or CA cert to validate, which this sketch does not maintain.
      _webSocket.beginSSL(LOG_SERVER_HOST, LOG_SERVER_PORT, LOG_SERVER_PATH);
#endif
   }

   ///
   /// <summary>
   /// Drives the WebSocket connection; call once per loop() iteration.
   /// </summary>
   ///
   void loop()
   {
      _webSocket.loop();
   }

   ///
   /// <summary>
   /// Returns whether the WebSocket connection to the LogServer is currently up.
   /// </summary>
   /// <returns>True if connected; otherwise false.</returns>
   ///
   bool isConnected() const
   {
      return _connected;
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
   ///
   void log(const char* message)
   {
      _sendOrQueue((std::string(message) + "\n").c_str());

      if (!_linePending)
      {
         Serial.print("Logger ----- ");
      }
      Serial.println(message);

      _linePending = false;
   }

   ///
   /// <summary>
   /// Overload of log(const char*) for callers holding a std::string.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   ///
   void log(const std::string& message)
   {
      log(message.c_str());
   }

   ///
   /// <summary>
   /// Overload of log(const char*) for callers holding an Arduino String.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   ///
   void log(const String& message)
   {
      log(message.c_str());
   }

   ///
   /// <summary>
   /// Sends a message fragment to the LogServer without completing the log entry,
   /// allowing the result to be appended later via a subsequent logPartial()/log() call
   /// (e.g. printing "WiFi... " now and "OK" once the connection result is known). The
   /// LogServer joins fragments into a single entry until one is sent with a trailing
   /// newline (see log()). Echoes to Serial the same way, without a trailing newline.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   ///
   void logPartial(const char* message)
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
   void logPartial(const std::string& message)
   {
      logPartial(message.c_str());
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*) for callers holding an Arduino String.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   ///
   void logPartial(const String& message)
   {
      logPartial(message.c_str());
   }
};

///
/// <summary>
/// Returns the single Logger instance shared by the whole sketch (one physical board, one
/// LogServer connection). Board-level helpers (see ArduinoBase::printlnInitStatus(),
/// initWifi(), initSensor(), initClient()) log through this automatically, so callers only
/// need to invoke those helpers once to update both the display/Serial and the LogServer.
/// </summary>
/// <returns>Reference to the global Logger instance.</returns>
///
inline Logger& logger()
{
   static Logger instance;
   return instance;
}
