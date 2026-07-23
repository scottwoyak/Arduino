#pragma once

#include <functional>
#include <string>

#include <WebSocketsClient.h>

WebSocketsClient webSocket;

using TelemetryOnConnectedFunc = std::function<void()>;
using TelemetryOnDisconnectedFunc = std::function<void()>;
using TelemetryOnReceiveTextFunc = std::function<void(const std::string&)>;
using TelemetryOnSendTextFunc = std::function<void(const std::string&)>;
using TelemetryOnErrorFunc = std::function<void(const std::string&)>;
using TelemetryOnStartedFunc = std::function<void()>;

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
   static constexpr unsigned long START_RETRY_INTERVAL_MS = 2000;

   std::string _serverVersion = "";
   std::string _status = "";
   std::string _topic;
   bool _started = false;
   bool _startRetryPending = false;
   unsigned long _startRetryAtMs = 0;

   // callbacks for our user
   TelemetryOnConnectedFunc _onConnectedFunc = nullptr;
   TelemetryOnDisconnectedFunc _onDisconnectedFunc = nullptr;
   TelemetryOnSendTextFunc _onSendTextFunc = nullptr;
   TelemetryOnReceiveTextFunc _onReceiveTextFunc = nullptr;
   TelemetryOnErrorFunc _onErrorFunc = nullptr;
   TelemetryOnStartedFunc _onStartedFunc = nullptr;

   // static instance for handling callbacks from WebSocketClient
   static TelemetryClient* _instance;

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
   ///
   virtual void _onDisconnected() = 0;

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
   /// Called once the start handshake (Publish/Subscribe) has succeeded. The default
   /// implementation does nothing.
   /// </summary>
   ///
   virtual void _onStarted() {};

   ///
   /// <summary>
   /// Sends a text message over the WebSocket connection and notifies the
   /// onSendText callback, if set.
   /// </summary>
   /// <param name="text">The message text to send.</param>
   ///
   void _sendText(const std::string& text)
   {
      webSocket.sendTXT(text.c_str());
      if (_onSendTextFunc)
      {
         _onSendTextFunc(text);
      }
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
         _onDisconnected();
         if (_onDisconnectedFunc)
         {
            _onDisconnectedFunc();
         }
         break;

      case WStype_CONNECTED:
         _onConnected();
         if (_onConnectedFunc)
         {
            _onConnectedFunc();
         }
         break;

      case WStype_TEXT:
      {
         //Serial.println((const char*)payload);
         std::string str = (const char*)payload;

         if (_serverVersion.length() == 0)
         {
            // the first message received is a simple greeting with the server version

            // strip off the initial part "TelemetryServer v###"
            _serverVersion = str.substr(std::string("TelemetryServer v").length());
            Serial.print("Server Version: ");
            Serial.println(_serverVersion.c_str());
         }
         else if (_status.length() == 0)
         {
            // the second message is a response to the subscribe/publish request
            _status = str;
            if (str.starts_with("ERR"))
            {
               Serial.print("Start failure: ");
               Serial.println(str.c_str());
               if (_onErrorFunc)
               {
                  _onErrorFunc(str);
               }

               // wait a bit, then retry the start request rather than giving up
               _startRetryPending = true;
               _startRetryAtMs = millis() + START_RETRY_INTERVAL_MS;
            }
            else
            {
               Serial.println("Started");
               _started = true;
               _onStarted();
               if (_onStartedFunc)
               {
                  _onStartedFunc();
               }
            }
         }
         else
         {
            _onText(str);
            if (_onReceiveTextFunc)
            {
               _onReceiveTextFunc(str);
            }
         }
      }
      break;

      case WStype_BIN:
         Serial.printf("[WS] Got Binary data\n");
         break;

      case WStype_PONG:
         Serial.printf("[WS] Pong\n");
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
   explicit TelemetryClient(const std::string& topic)
   {
      _instance = this;
      _topic = topic;
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
   /// Services the WebSocket connection and any pending start-retry. Must be called
   /// regularly from the sketch's loop().
   /// </summary>
   ///
   void loop()
   {
      if (_startRetryPending && millis() >= _startRetryAtMs)
      {
         _startRetryPending = false;
         _status.clear();
         _onConnected();
      }

      _onLoop();
      webSocket.loop();
   }

   ///
   /// <summary>
   /// Sets optional user callbacks invoked on connection, disconnection, sent/received
   /// text, errors, and successful start.
   /// </summary>
   /// <param name="onConnectedFunc">Called when the WebSocket connection is established.</param>
   /// <param name="onDisconnectedFunc">Called when the WebSocket connection is lost.</param>
   /// <param name="onSendTextFunc">Called whenever a text message is sent.</param>
   /// <param name="onReceiveTextFunc">Called whenever a text message is received.</param>
   /// <param name="onErrorFunc">Called when the start/subscribe handshake fails.</param>
   /// <param name="onStartedFunc">Called when the start/subscribe handshake succeeds.</param>
   ///
   void setCallbacks(
      TelemetryOnConnectedFunc onConnectedFunc,
      TelemetryOnDisconnectedFunc onDisconnectedFunc,
      TelemetryOnSendTextFunc onSendTextFunc,
      TelemetryOnReceiveTextFunc onReceiveTextFunc,
      TelemetryOnErrorFunc onErrorFunc,
      TelemetryOnStartedFunc onStartedFunc
   )
   {
      _onConnectedFunc = onConnectedFunc;
      _onDisconnectedFunc = onDisconnectedFunc;
      _onSendTextFunc = onSendTextFunc;
      _onReceiveTextFunc = onReceiveTextFunc;
      _onErrorFunc = onErrorFunc;
      _onStartedFunc = onStartedFunc;
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

   void _onDisconnected() override
   {
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
            _sendText(value.c_str());
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
   ///
   TelemetryPublisher(const std::string& topic, uint8_t decimalPlaces) : TelemetryClient(topic)
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

   void _onDisconnected() override
   {
   }

   void _onConnected() override
   {
      // subscribe
      std::string cmd = "Subscribe " + getTopic();
      _sendText(cmd.c_str());

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
   ///
   TelemetrySubscriber(const std::string& topic) : TelemetryClient(topic)
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
