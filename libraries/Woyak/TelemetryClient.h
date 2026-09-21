#pragma once

#include <string>

#include <WebSocketsClient.h>

#include "RollingRate.h"
#include "Status.h"
#include "Util.h"
#include "Logger.h"

// Display-based error rendering is only available on boards with a display.
// ARDUINO_DISPLAY_SUPPORTED is defined by ArduinoBoard.h when the target board has one.
#ifdef ARDUINO_DISPLAY_SUPPORTED
#include "ArduinoWithDisplay.h"
#endif

WebSocketsClient webSocket;

///
/// <summary>
/// Number of seconds to wait before resetting the device after a telemetry error or
/// disconnect, giving Serial/display output time to be seen.
/// </summary>
///
constexpr float TELEMETRY_RESET_DELAY_S = 10.0f;

///
/// <summary>
/// Number of samples used by TelemetryClient's rolling message rate tracker.
/// </summary>
///
constexpr uint16_t TELEMETRY_RATE_NUM_SAMPLES = 50;

///
/// <summary>
/// Handles telemetry client lifecycle events (connect, disconnect, start, error, and
/// sent/received text). Provides default behavior for each event; sketches that need
/// custom behavior should derive from this class and override only the methods they
/// need. Contract: every override must call the base class implementation (typically
/// first) so any current or future logic added to the base method (e.g. status
/// updates, disconnect/reset handling) is never silently skipped.
/// </summary>
///
class TelemetryEventHandler
{
private:
   IStatus* _status;
#ifdef ARDUINO_DISPLAY_SUPPORTED
   ArduinoWithDisplay* _display;
#endif

public:
   ///
   /// <summary>
   /// Initializes a new instance of the TelemetryEventHandler class.
   /// </summary>
   /// <param name="status">Status indicator updated by the default event handling.</param>
#ifdef ARDUINO_DISPLAY_SUPPORTED
   /// <param name="display">Optional display used to draw error messages by default onError() handling.</param>
#endif
   ///
#ifdef ARDUINO_DISPLAY_SUPPORTED
   explicit TelemetryEventHandler(IStatus* status, ArduinoWithDisplay* display = nullptr) : _status(status), _display(display)
   {
   }
#else
   explicit TelemetryEventHandler(IStatus* status) : _status(status)
   {
   }
#endif

   virtual ~TelemetryEventHandler() = default;

   ///
   /// <summary>
   /// Completes the "Telemetry..." label printed by ArduinoBase::initClient() with
   /// "OK, v<version>", reporting the server's version greeting to Serial and, on
   /// display-capable boards, the display as well. Called by TelemetryClient; not
   /// virtual since it's not an event a sketch would want to customize.
   /// </summary>
   /// <param name="version">Server version reported by the telemetry server</param>
   ///
   void printServerVersion(const std::string& version)
   {
      std::string result = "OK, v" + version;

      Logger.log(result);

#ifdef ARDUINO_DISPLAY_SUPPORTED
      if (_display != nullptr)
      {
         _display->printlnR(result, Color::VALUE);
      }
#endif
   }

   ///
   /// <summary>
   /// Invoked when the telemetry WebSocket connection is established. Default
   /// implementation does nothing.
   /// </summary>
   ///
   virtual void onConnected()
   {
   }

   ///
   /// <summary>
   /// Invoked when the telemetry client finishes starting up. Default implementation
   /// sets the status to READY. The "Telemetry..." label printed by
   /// ArduinoBase::initClient() is already completed by printServerVersion() once the
   /// server's version greeting arrives, so this method does not print anything further.
   /// Overrides must call this base implementation (see class remarks).
   /// </summary>
   ///
   virtual void onStarted()
   {
      _status->setStatus(Status::READY);
   }

   ///
   /// <summary>
   /// Invoked when the telemetry WebSocket connection is lost. Default implementation
   /// logs the reason, shows a standalone message on the display (if one was supplied)
   /// since a disconnect can happen at any time and not just while the "Telemetry..."
   /// label is still on screen, sets the status to FAILED, and resets the device.
   /// Overrides must call this base implementation (see class remarks).
   /// </summary>
   /// <param name="reason">Reason for the disconnect, as reported by the telemetry client</param>
   ///
   virtual void onDisconnected(const std::string& reason)
   {
      Logger.log("Telemetry connection lost (" + String(reason.c_str()) + "). Restarting in " + String(int(TELEMETRY_RESET_DELAY_S)) + "s", LogSeverity::ERROR);

#ifdef ARDUINO_DISPLAY_SUPPORTED
      if (_display != nullptr)
      {
         // Unlike onConnectionFailed(), a disconnect can happen at any time after a
         // successful connection, not just while the "Telemetry..." label printed by
         // ArduinoBase::initClient() is still on screen, so print a standalone message
         // instead of assuming there's a label row to complete.
         _display->setTextSize(2);
         _display->clearDisplay();
         _display->display.setTextWrap(true);
         _display->println("Telemetry connection lost", Color::RED);
         _display->println(reason, Color::RED);
      }
#endif

      _status->setStatus(Status::FAILED);
      Util::reset(TELEMETRY_RESET_DELAY_S);
   }

   ///
   /// <summary>
   /// Invoked when the WebSocket never successfully connected (e.g. the server isn't
   /// running, or it's unreachable) before the socket was torn down. Default
   /// implementation logs a clearer message than the raw low-level socket teardown
   /// reason, completes the "Telemetry..." label (if a display was supplied) with
   /// "FAILED", sets the status to FAILED, and resets the device. Overrides must call
   /// this base implementation (see class remarks).
   /// </summary>
   /// <param name="reason">Low-level reason reported by the telemetry client, if any</param>
   ///
   virtual void onConnectionFailed(const std::string& reason)
   {
      Logger.log("Could not connect to telemetry server (" + String(reason.c_str()) + "). Restarting in " + String(int(TELEMETRY_RESET_DELAY_S)) + "s", LogSeverity::ERROR);

#ifdef ARDUINO_DISPLAY_SUPPORTED
      if (_display != nullptr)
      {
         // Completes the "Telemetry..." label printed by ArduinoBase::initClient() -
         // see the matching comment in onStarted() above.
         _display->printlnR("FAILED", Color::RED);
         _display->println(reason, Color::RED);
      }
#endif

      _status->setStatus(Status::FAILED);
      Util::reset(TELEMETRY_RESET_DELAY_S);
   }

   ///
   /// <summary>
   /// Invoked when the telemetry client reports an error. Default implementation logs
   /// the message, draws it on the display (if one was supplied), sets the status to
   /// FAILED, and resets the device.
   /// </summary>
   /// <param name="message">Error message reported by the telemetry client</param>
   ///
   virtual void onError(const std::string& message)
   {
      Logger.log(message, LogSeverity::ERROR);

#ifdef ARDUINO_DISPLAY_SUPPORTED
      if (_display != nullptr)
      {
         _display->setTextSize(2);
         _display->clearDisplay();
         _display->display.setTextWrap(true);
         _display->println(message, Color::RED);
      }
#endif

      _status->setStatus(Status::FAILED);
      Util::reset(TELEMETRY_RESET_DELAY_S);
   }

   ///
   /// <summary>
   /// Invoked whenever a text message is sent. Default implementation does nothing;
   /// Serial echo logging of sent messages is handled independently by
   /// TelemetryClient and isn't tied to this override, so subclasses don't need to
   /// call the base implementation to preserve it.
   /// </summary>
   /// <param name="message">The message text that was sent</param>
   ///
   virtual void onSendText(const std::string& message)
   {
      (void)message;
   }

   ///
   /// <summary>
   /// Invoked whenever a text message is received. Default implementation does
   /// nothing; Serial echo logging of received messages is handled independently by
   /// TelemetryClient and isn't tied to this override, so subclasses don't need to
   /// call the base implementation to preserve it.
   /// </summary>
   /// <param name="message">The message text that was received</param>
   ///
   virtual void onReceiveText(const std::string& message)
   {
      (void)message;
   }
};

///
/// <summary>
/// Base class for WebSocket-based telemetry clients. Manages the connection lifecycle,
/// the start/subscribe handshake with the telemetry server, and optional user callbacks.
/// Subclasses (TelemetryPublisher, TelemetrySubscriber) implement the specific handshake
/// and message handling behavior.
/// </summary>
///
class TelemetryClient
{
private:
   std::string _serverVersion = "";
   std::string _status = "";
   std::string _topic;
   bool _started = false;
   bool _hasConnected = false;
   bool _echoEnabled = false;
   RollingRate _rate{ TELEMETRY_RATE_NUM_SAMPLES };

   // user event handler; owned by this instance only when no handler was supplied
   TelemetryEventHandler* _handler = nullptr;
   bool _ownsHandler = false;

   // static instance for handling callbacks from WebSocketClient
   static TelemetryClient* _instance;

   ///
   /// <summary>
   /// Replaces all occurrences of a substring within a string, in place.
   /// </summary>
   /// <param name="str">String to modify</param>
   /// <param name="from">Substring to search for</param>
   /// <param name="to">Replacement substring</param>
   ///
   static void _replaceAll(std::string& str, const std::string& from, const std::string& to)
   {
      if (from.empty())
      {
         return;
      }

      size_t startPos = 0;
      while ((startPos = str.find(from, startPos)) != std::string::npos)
      {
         str.replace(startPos, from.length(), to);
         startPos += to.length(); // Move past the new replacement
      }
   }

   ///
   /// <summary>
   /// Logs a sent/received text message to Serial, quoted and with embedded newlines
   /// escaped for single-line readability.
   /// </summary>
   /// <param name="prefix">Direction prefix to print before the quoted message (e.g. ">>> ").</param>
   /// <param name="message">The message text to log.</param>
   ///
   static void _echoText(const char* prefix, const std::string& message)
   {
      std::string escaped = message;
      _replaceAll(escaped, "\n", "\\n");
      Serial.println((prefix + ("\"" + escaped + "\"")).c_str());
   }

   ///
   /// <summary>
   /// Static trampoline that forwards WebSocketsClient events to the singleton
   /// TelemetryClient instance's _onEvent().
   /// </summary>
   /// <param name="type">The event type reported by WebSocketsClient.</param>
   /// <param name="payload">The event payload, if any.</param>
   /// <param name="length">The length of the payload, in bytes.</param>
   ///
   static void webSocketSubscriberEvent(WStype_t type, uint8_t* payload, size_t length)
   {
      _instance->_onEvent(type, payload, length);
   }

protected:
   ///
   /// <summary>
   /// Called when the WebSocket connection is lost. Subclasses should reset any
   /// per-connection state here.
   /// </summary>
   /// <param name="reason">The disconnect reason reported by WebSocketsClient, if any.</param>
   ///
   virtual void _onDisconnected(const std::string& reason) = 0;

   ///
   /// <summary>
   /// Called when the WebSocket connection is established. Subclasses should send
   /// the initial handshake command (e.g. Publish/Subscribe) here.
   /// </summary>
   ///
   virtual void _onConnected() = 0;

   ///
   /// <summary>
   /// Called for each text message received after the start handshake has completed.
   /// The default implementation does nothing.
   /// </summary>
   /// <param name="payload">The received message text.</param>
   ///
   virtual void _onText(const std::string& payload) { (void)payload; };

   ///
   /// <summary>
   /// Called once per loop() call, after any pending start retry has been handled.
   /// The default implementation does nothing.
   /// </summary>
   ///
   virtual void _onLoop() {};

   ///
   /// <summary>
   /// Records a tick for the rolling message rate tracker. Called just before the
   /// handler's onSendText() so the rate reflects how often requests are made to the
   /// server (each publish for TelemetryPublisher, each "get" for TelemetrySubscriber).
   /// </summary>
   ///
   void tickRate()
   {
      _rate.tick();
   }

   ///
   /// <summary>
   /// Called once the start handshake (Publish/Subscribe) has succeeded. The default
   /// implementation does nothing.
   /// </summary>
   ///
   virtual void _onStarted() {};

   ///
   /// <summary>
   /// Sends a text message over the WebSocket connection, echoes it to Serial (if
   /// enabled), and notifies the onSendText callback, if set.
   /// </summary>
   /// <param name="text">The message text to send.</param>
   ///
   void _sendText(const std::string& text)
   {
      webSocket.sendTXT(text.c_str());
      tickRate();

      if (_echoEnabled)
      {
         _echoText(">>> ", text);
      }

      _handler->onSendText(text);
   }

   ///
   /// <summary>
   /// Overload of _sendText(const std::string&) accepting a String so callers don't
   /// need to call .c_str() themselves.
   /// </summary>
   /// <param name="text">The message text to send.</param>
   ///
   void _sendText(const String& text)
   {
      _sendText(std::string(text.c_str()));
   }

   ///
   /// <summary>
   /// Overload of _sendText(const std::string&) accepting a const char* to disambiguate
   /// string-literal calls between the std::string and String overloads.
   /// </summary>
   /// <param name="text">The message text to send.</param>
   ///
   void _sendText(const char* text)
   {
      _sendText(std::string(text));
   }

   ///
   /// <summary>
   /// Handles a raw WebSocketsClient event: dispatches connect/disconnect notifications,
   /// and for text messages, processes the server version greeting, the start/subscribe
   /// handshake response, or forwards the payload to _onText().
   /// </summary>
   /// <param name="type">The event type reported by WebSocketsClient.</param>
   /// <param name="payload">The event payload, if any.</param>
   /// <param name="length">The length of the payload, in bytes.</param>
   ///
   void _onEvent(WStype_t type, uint8_t* payload, size_t length)
   {
      switch (type)
      {
      case WStype_DISCONNECTED:
      {
         std::string reason = (payload != nullptr && length > 0) ? std::string((const char*)payload, length) : "";

         // reset handshake state so the greeting and start/publish ack are
         // recognized and re-processed correctly after reconnecting, instead
         // of being routed to _onText() as ordinary payload data
         _serverVersion.clear();
         _status.clear();
         _started = false;

         _onDisconnected(reason);

         if (_hasConnected)
         {
            _hasConnected = false;
            _handler->onDisconnected(reason);
         }
         else
         {
            _handler->onConnectionFailed(reason);
         }
      }
      break;

      case WStype_CONNECTED:
         // send the start/publish/subscribe handshake immediately; if it fails
         // (e.g. topic still in use), the caller's onError callback is responsible
         // for deciding how to recover (e.g. resetting the device after a delay).
         _hasConnected = true;
         _status.clear();
         _onConnected();
         _handler->onConnected();
         break;

      case WStype_TEXT:
      {
         //Serial.println((const char*)payload);
         std::string str = (const char*)payload;

         if (_serverVersion.length() == 0)
         {
            // the first message received is a simple greeting with the server version;
            // reported via the handler, which completes the "Telemetry..." label
            // printed by ArduinoBase::initClient()

            // strip off the initial part "TelemetryServer v###"
            _serverVersion = str.substr(std::string("TelemetryServer v").length());

            _handler->printServerVersion(_serverVersion);
         }
         else if (_status.length() == 0)
         {
            // the second message is a response to the subscribe/publish request
            _status = str;
            if (str.starts_with("ERR"))
            {
               _handler->onError("Start failure: " + str);
            }
            else
            {
               _started = true;
               _rate.reset();
               _onStarted();
               _handler->onStarted();
            }
         }
         else
         {
            if (_echoEnabled)
            {
               _echoText("<<< ", str);
            }

            _onText(str);
            _handler->onReceiveText(str);
         }
      }
      break;

      case WStype_BIN:
         Serial.printf("[WS] Got Binary data\n");
         break;

      case WStype_PING:
      case WStype_PONG:
         // Keepalive frames; nothing to do.
         break;

      default:
         Serial.print("Unhandled Socket Event Type: ");
         Serial.println(type);
         break;
      }
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the TelemetryClient class.
   /// </summary>
   /// <param name="topic">The telemetry topic to publish or subscribe to.</param>
   ///
   explicit TelemetryClient(const std::string& topic, IStatus* status = nullptr, TelemetryEventHandler* handler = nullptr)
   {
      _instance = this;
      _topic = topic;

      if (handler != nullptr)
      {
         _handler = handler;
      }
      else
      {
         _handler = new TelemetryEventHandler(status);
         _ownsHandler = true;
      }
   }

   ~TelemetryClient()
   {
      if (_ownsHandler)
      {
         delete _handler;
      }
   }

   ///
   /// <summary>
   /// Gets the telemetry topic this client publishes or subscribes to.
   /// </summary>
   /// <returns>The topic name.</returns>
   ///
   const std::string& getTopic() const
   {
      return _topic;
   }

   ///
   /// <summary>
   /// Replaces the event handler used by this client. Any previously owned default
   /// handler (created when no handler was supplied to the constructor) is deleted.
   /// Useful when the handler needs a reference to the client itself, since the
   /// handler can be constructed and assigned after the client, avoiding a forward
   /// declaration of the client in the sketch.
   /// </summary>
   /// <param name="handler">The new event handler; must not be nullptr.</param>
   ///
   void setHandler(TelemetryEventHandler* handler)
   {
      ASSERT(handler != nullptr);

      if (_ownsHandler)
      {
         delete _handler;
      }

      _handler = handler;
      _ownsHandler = false;
   }

   ///
   /// <summary>
   /// Gets the URL of the connected WebSocket server.
   /// </summary>
   /// <returns>The server URL.</returns>
   ///
   std::string getUrl() const
   {
      return webSocket.getUrl().c_str();
   }

   ///
   /// <summary>
   /// Gets whether the start/subscribe handshake has completed successfully.
   /// </summary>
   /// <returns>True if started; false otherwise.</returns>
   ///
   bool isStarted() const
   {
      return _started;
   }

   ///
   /// <summary>
   /// Enables or disables Serial echo logging of sent/received text messages.
   /// Disabled by default.
   /// </summary>
   /// <param name="enabled">True to log sent/received text messages to Serial, false to suppress them.</param>
   ///
   void setEchoEnabled(bool enabled)
   {
      _echoEnabled = enabled;
   }

   ///
   /// <summary>
   /// Gets whether Serial echo logging of sent/received text is currently enabled.
   /// </summary>
   /// <returns>True if sent/received text messages are logged to Serial.</returns>
   ///
   bool isEchoEnabled() const
   {
      return _echoEnabled;
   }

   ///
   /// <summary>
   /// Gets whether the WebSocket connection is currently active.
   /// </summary>
   /// <returns>True if connected; false otherwise.</returns>
   ///
   bool isConnected() const
   {
      return webSocket.isConnected();
   }

   ///
   /// <summary>
   /// Gets the current message rate, in messages per second, based on the ticks
   /// recorded for each received message.
   /// </summary>
   /// <returns>Messages per second, or 0 if not enough data has been collected yet.</returns>
   ///
   float getRate() const
   {
      return _rate.get();
   }

   ///
   /// <summary>
   /// Gets the server version reported in the initial greeting message.
   /// </summary>
   /// <returns>The server version string, or empty if not yet received.</returns>
   ///
   const std::string& getServerVersion() const
   {
      return _serverVersion;
   }

   ///
   /// <summary>
   /// Gets the status text returned by the server in response to the start/subscribe
   /// handshake.
   /// </summary>
   /// <returns>The status text, or empty if not yet received.</returns>
   ///
   const std::string& getStatus() const
   {
      return _status;
   }

   ///
   /// <summary>
   /// Connects to the telemetry server over an unencrypted WebSocket connection.
   /// </summary>
   /// <param name="webSocketServerHost">The server hostname or IP address.</param>
   /// <param name="webSocketServerPort">The server port.</param>
   /// <param name="webSocketPath">The WebSocket path.</param>
   ///
   void begin(const char* webSocketServerHost, uint16_t webSocketServerPort, const char* webSocketPath = "/ws")
   {
      webSocket.begin(webSocketServerHost, webSocketServerPort, webSocketPath);
      webSocket.onEvent(webSocketSubscriberEvent);
   }

   ///
   /// <summary>
   /// Connects to the telemetry server over an encrypted (SSL) WebSocket connection.
   /// </summary>
   /// <param name="webSocketServerHost">The server hostname or IP address.</param>
   /// <param name="webSocketServerPort">The server port.</param>
   /// <param name="webSocketPath">The WebSocket path.</param>
   ///
   void beginSSL(const char* webSocketServerHost, uint16_t webSocketServerPort, const char* webSocketPath = "/ws")
   {
      webSocket.beginSSL(webSocketServerHost, webSocketServerPort, webSocketPath);
      webSocket.onEvent(webSocketSubscriberEvent);
   }

   ///
   /// <summary>
   /// Services the WebSocket connection. Must be called regularly from the sketch's
   /// loop().
   /// </summary>
   ///
   void loop()
   {
      _onLoop();
      webSocket.loop();
   }
};

///
/// <summary>
/// TelemetryClient specialization that periodically publishes a value to a topic on
/// the telemetry server, resending only when the value changes.
/// </summary>
///
class TelemetryPublisher : public TelemetryClient
{
private:
   uint8_t _decimalPlaces;
   float _value = NAN;
   std::string _lastValue = "";
   bool _ready = false;

   void _onDisconnected(const std::string& reason) override
   {
      (void)reason;
      _ready = false;
      _lastValue = "";
   }

   void _onConnected() override
   {
      // initialize
      std::string cmd = "Publish " + getTopic();
      _sendText(cmd);
   }

   void _onText(const std::string& payload) override
   {
      // the 'ok' response from us sending a value
      (void)payload;
      _ready = true;
   }

   void _onStarted() override
   {
      _ready = true;
   }

   void _onLoop() override
   {
      if (_ready)
      {
         String value(_value, (unsigned int)_decimalPlaces);

         if (value != _lastValue.c_str())
         {
            _sendText(value);
            _lastValue = value.c_str();
            _ready = false;
         }
      }
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the TelemetryPublisher class.
   /// </summary>
   /// <param name="topic">The telemetry topic to publish to.</param>
   /// <param name="decimalPlaces">The number of decimal places to publish values with.</param>
   /// <param name="status">Status indicator used by the default event handler, if no handler is supplied.</param>
   /// <param name="handler">Event handler for connection lifecycle events, or nullptr to use a default handler.</param>
   ///
   TelemetryPublisher(const std::string& topic, uint8_t decimalPlaces, IStatus* status = nullptr, TelemetryEventHandler* handler = nullptr) : TelemetryClient(topic, status, handler)
   {
      _decimalPlaces = decimalPlaces;
   }

   ///
   /// <summary>
   /// Sets the value to be published on the next loop() call, if it has changed.
   /// </summary>
   /// <param name="value">The value to publish.</param>
   ///
   void setValue(float value)
   {
      _value = value;
   }
};


///
/// <summary>
/// TelemetryClient specialization that subscribes to a topic on the telemetry server
/// and continuously requests the latest published value.
/// </summary>
///
class TelemetrySubscriber : public TelemetryClient
{
private:
   float _value = NAN;

   void _onDisconnected(const std::string& reason) override
   {
      (void)reason;
   }

   void _onConnected() override
   {
      // subscribe
      std::string cmd = "Subscribe " + getTopic();
      _sendText(cmd);

      // request the first value
      _sendText("get");
   }

   void _onText(const std::string& payload) override
   {
      try
      {
         _value = std::stof(payload);
      }
      catch (const std::exception& e)
      {
         _value = NAN;
      }

      // request the next value
      _sendText("get");
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the TelemetrySubscriber class.
   /// </summary>
   /// <param name="topic">The telemetry topic to subscribe to.</param>
   /// <param name="status">Status indicator used by the default event handler, if no handler is supplied.</param>
   /// <param name="handler">Event handler for connection lifecycle events, or nullptr to use a default handler.</param>
   ///
   TelemetrySubscriber(const std::string& topic, IStatus* status = nullptr, TelemetryEventHandler* handler = nullptr) : TelemetryClient(topic, status, handler)
   {
   }

   ///
   /// <summary>
   /// Gets the most recently received value for the subscribed topic.
   /// </summary>
   /// <returns>The latest value, or NAN if none has been received yet.</returns>
   ///
   float getValue()
   {
      return _value;
   }
};

TelemetryClient* TelemetryClient::_instance;
