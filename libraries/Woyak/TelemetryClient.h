#pragma once

#include <WebSocketsClient.h>
#include <Util.h>

WebSocketsClient webSocket;

typedef std::function<void()> TelemetryOnConnectedFunc;
typedef std::function<void()> TelemetryOnDisconnectedFunc;
typedef std::function<void(std::string)> TelemetryOnTextFunc;
typedef std::function<void(std::string)> TelemetryOnErrorFunc;
typedef std::function<void()> TelemetryOnStartedFunc;

//-------------------------------------------------------------------------------------------------
class TelemetryClient
{
private:
   std::string _serverVersion = "";
   std::string _status = "";
   std::string _topic;

   // callbacks for our user
   TelemetryOnConnectedFunc _onConnectedFunc = nullptr;
   TelemetryOnDisconnectedFunc _onDisconnectedFunc = nullptr;
   TelemetryOnTextFunc _onTextFunc = nullptr;
   TelemetryOnErrorFunc _onErrorFunc = nullptr;
   TelemetryOnStartedFunc _onStartedFunc = nullptr;

   // static instance for handling callbacks from WebSocketClient
   static TelemetryClient* _instance;

   static void webSocketSubscriberEvent(WStype_t type, uint8_t* payload, size_t length)
   {
      _instance->_onEvent(type, payload, length);
   }

protected:
   virtual void _onDisconnected() = 0;
   virtual void _onConnected() = 0;
   virtual void _onText(std::string) {};
   virtual void _onLoop() {};
   virtual void _onStarted() {};

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
            }
            else
            {
               Serial.println("Started");
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
            if (_onTextFunc)
            {
               _onTextFunc(str);
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
   TelemetryClient(std::string topic)
   {
      _instance = this;
      _topic = topic;
   }

   std::string getTopic()
   {
      return _topic;
   }

   std::string getUrl()
   {
      return webSocket.getUrl().c_str();
   }

   bool isConnected()
   {
      return webSocket.isConnected();
   }

   std::string getServerVersion()
   {
      return _serverVersion;
   }

   std::string getStatus()
   {
      return _status;
   }

   void begin(const char* webSocketServerHost, uint16_t webSocketServerPort, const char* webSocketPath="/ws")
   {
      webSocket.begin(webSocketServerHost, webSocketServerPort, webSocketPath);
      webSocket.onEvent(webSocketSubscriberEvent);
   }

   void beginSSL(const char* webSocketServerHost, uint16_t webSocketServerPort, const char* webSocketPath="/ws")
   {
      webSocket.beginSSL(webSocketServerHost, webSocketServerPort, webSocketPath);
      webSocket.onEvent(webSocketSubscriberEvent);
   }

   void loop()
   {
      _onLoop();
      webSocket.loop();
   }

   void setCallbacks(
      TelemetryOnConnectedFunc onConnectedFunc = nullptr,
      TelemetryOnDisconnectedFunc onDisconnectedFunc = nullptr,
      TelemetryOnTextFunc onTextFunc = nullptr,
      TelemetryOnErrorFunc onErrFunc = nullptr,
      TelemetryOnStartedFunc onStartedFunc = nullptr
   )
   {
      _onConnectedFunc = onConnectedFunc;
      _onDisconnectedFunc = onDisconnectedFunc;
      _onTextFunc = onTextFunc;
      _onErrorFunc = onErrFunc;
      _onStartedFunc = onStartedFunc;
   }
};

//-------------------------------------------------------------------------------------------------
class TelemetryPublisher : public TelemetryClient
{
private:
   uint8_t _decimalPlaces;
   float _value = NAN;
   std::string _lastValue = "";
   bool _ready = false;

   virtual void _onDisconnected()
   {
      _ready = false;
      _lastValue = "";
   }

   virtual void _onConnected()
   {
      // initialize
      std::string cmd = "Publish " + getTopic();
      webSocket.sendTXT(cmd.c_str());
   }

   virtual void _onText(std::string payload)
   {
      // the 'ok' response from us sending a value
      _ready = true;
   }

   virtual void _onStarted()
   {
      _ready = true;
   }

   virtual void _onLoop()
   {
      if (_ready)
      {
         String value(_value, (unsigned int)_decimalPlaces);

         if (value != _lastValue.c_str())
         {
            webSocket.sendTXT(value.c_str());
            _lastValue = value.c_str();
            _ready = false;
         }
      }
   }

public:
   TelemetryPublisher(std::string topic, uint8_t decimalPlaces) : TelemetryClient(topic)
   {
      _decimalPlaces = decimalPlaces;
   }

   void setValue(float value)
   {
      _value = value;
   }
};


//-------------------------------------------------------------------------------------------------
class TelemetrySubscriber : public TelemetryClient
{
private:
   float _value = NAN;

   virtual void _onDisconnected()
   {
   }

   virtual void _onConnected()
   {
      // subscribe
      std::string cmd = "Subscribe " + getTopic();
      webSocket.sendTXT(cmd.c_str());

      // request the first value
      webSocket.sendTXT("get");
   }

   virtual void _onText(std::string payload)
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
      webSocket.sendTXT("get");
   }

public:
   TelemetrySubscriber(std::string topic) : TelemetryClient(topic)
   {
   }

   float getValue()
   {
      return _value;
   }
};

TelemetryClient* TelemetryClient::_instance;
