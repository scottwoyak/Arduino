#pragma once

#include <WebSocketsClient.h>
#include <Util.h>

WebSocketsClient webSocket;

typedef std::function<void()> WebSocketConnectedFunc;
typedef std::function<void()> WebSocketDisconnectedFunc;

//-------------------------------------------------------------------------------------------------
class TelemetryClient
{
private:
   WebSocketConnectedFunc _onConnectedFunc;
   WebSocketDisconnectedFunc _onDisconnectedFunc;

   static TelemetryClient* _instance;

   static void webSocketSubscriberEvent(WStype_t type, uint8_t* payload, size_t length)
   {
      _instance->_onEvent(type, payload, length);
   }

protected:
   virtual void _onDisconnected() = 0;
   virtual void _onConnected() = 0;
   virtual void _onText(uint8_t* payload, size_t length) = 0;

   void _onEvent(WStype_t type, uint8_t* payload, size_t length)
   {
      switch (type)
      {
      case WStype_DISCONNECTED:
         Serial.print("[WS] Disconnected!\n");
         _onDisconnected();
         _onDisconnectedFunc();
         break;

      case WStype_CONNECTED:
         Serial.print("[WS] Connected!\n");
         _onConnected();
         _onConnectedFunc();
         break;

      case WStype_TEXT:
         //Serial.println((const char*)payload);
         _onText(payload, length);
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
   TelemetryClient(WebSocketConnectedFunc connectedFunc, WebSocketDisconnectedFunc disconnectedFunc)
   {
      _onConnectedFunc = connectedFunc;
      _onDisconnectedFunc = disconnectedFunc;
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

      // Set up ping/pong
      webSocket.setReconnectInterval(5000);
   }

   void loop()
   {
      webSocket.loop();
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
      webSocket.sendTXT("Subscribe ArduinoTest");

      // request the first value
      webSocket.sendTXT("get");
   }

   virtual void _onText(uint8_t* payload, size_t length)
   {
      _value = std::stof((const char*)payload);

      // request the next value
      webSocket.sendTXT("get");
   }

public:
   TelemetrySubscriber(WebSocketConnectedFunc connectedFunc, WebSocketDisconnectedFunc disconnectedFunc) : TelemetryClient(connectedFunc, disconnectedFunc)
   {
   }

   float getValue()
   {
      return _value;
   }
};

TelemetryClient* TelemetryClient::_instance;
