#pragma once

#include <WebSocketsClient.h>
#include <Util.h>

WebSocketsClient webSocket;

typedef std::function<void()> WebSocketOnConnectedFunc;
typedef std::function<void()> WebSocketOnDisconnectedFunc;
typedef std::function<void(uint8_t* payload, size_t length)> WebSocketOnTextFunc;

//-------------------------------------------------------------------------------------------------
class TelemetryClient
{
private:
   // callbacks for our user
   WebSocketOnConnectedFunc _onConnectedFunc = nullptr;
   WebSocketOnDisconnectedFunc _onDisconnectedFunc = nullptr;
   WebSocketOnTextFunc _onTextFunc = nullptr;

   // static instance for handling callbacks from WebSocketClient
   static TelemetryClient* _instance;

   static void webSocketSubscriberEvent(WStype_t type, uint8_t* payload, size_t length)
   {
      _instance->_onEvent(type, payload, length);
   }

protected:
   virtual void _onDisconnected() = 0;
   virtual void _onConnected() = 0;
   virtual void _onText(uint8_t* payload, size_t length) = 0;
   virtual void _onLoop() {};

   void _onEvent(WStype_t type, uint8_t* payload, size_t length)
   {
      switch (type)
      {
      case WStype_DISCONNECTED:
         Serial.print("[WS] Disconnected!\n");
         _onDisconnected();
         if (_onDisconnectedFunc)
         {
            _onDisconnectedFunc();
         }
         break;

      case WStype_CONNECTED:
         Serial.print("[WS] Connected!\n");
         _onConnected();
         if (_onConnectedFunc)
         {
            _onConnectedFunc();
         }
         break;

      case WStype_TEXT:
         //Serial.println((const char*)payload);
         _onText(payload, length);
         if (_onTextFunc)
         {
            _onTextFunc(payload, length);
         }
         break;

      case WStype_BIN:
         Serial.printf("[WS] Got Binary data\n");
         break;

      case WStype_PONG:
         break;

      default:
         Serial.print("Unhandled Socket Event Type: ");
         Serial.println(type);
         break;
      }
   }

public:
   TelemetryClient()
   {
      _instance = this;
   }

   bool isConnected()
   {
      return webSocket.isConnected();
   }

   void begin(const char* webSocketServerHost, uint16_t webSocketServerPort, const char* webSocketPath)
   {
      webSocket.begin(webSocketServerHost, webSocketServerPort, webSocketPath);

      // Assign event handler
      webSocket.onEvent(webSocketSubscriberEvent);
   }

   void loop()
   {
      _onLoop();
      webSocket.loop();
   }

   void setCallbacks(WebSocketOnConnectedFunc connectedFunc = nullptr,
      WebSocketOnDisconnectedFunc disconnectedFunc = nullptr,
      WebSocketOnTextFunc textFunc = nullptr)
   {
      _onConnectedFunc = connectedFunc;
      _onDisconnectedFunc = disconnectedFunc;
      _onTextFunc = textFunc;
   }
};

//-------------------------------------------------------------------------------------------------
class TelemetryPublisher : public TelemetryClient
{
private:
   String _topic;
   uint8_t _decimalPlaces;
   float _value = NAN;
   String _lastValue = "";
   bool _ready = false;

   virtual void _onDisconnected()
   {
      _ready = false;
      _lastValue = "";
   }

   virtual void _onConnected()
   {
      // TODO use the provided topic
      // subscribe
      webSocket.sendTXT("Publish ArduinoTest");
   }

   virtual void _onText(uint8_t* payload, size_t length)
   {
      _ready = true;
   }

   virtual void _onLoop()
   {
      if (_ready)
      {
         String value(_value, (unsigned int)_decimalPlaces);

         if (value != _lastValue)
         {
            webSocket.sendTXT(value.c_str());
            _lastValue = value;
            _ready = false;
         }
      }
   }

public:
   TelemetryPublisher(String topic, uint8_t decimalPlaces)
   {
      _topic = topic;
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
   String _topic;

   virtual void _onDisconnected()
   {
   }

   virtual void _onConnected()
   {
      // subscribe
      webSocket.sendTXT("Subscribe ArduinoTest");

      // request the first value
      webSocket.sendTXT("get");
   }

   virtual void _onText(uint8_t* payload, size_t length)
   {
      try
      {
         _value = std::stof((const char*)payload);
      }
      catch (const std::exception& e)
      {
         _value = NAN;
      }

      // request the next value
      webSocket.sendTXT("get");
   }

public:
   TelemetrySubscriber(String topic)
   {
      _topic = topic;
   }

   float getValue()
   {
      return _value;
   }
};

TelemetryClient* TelemetryClient::_instance;
