#pragma once

#include <string>

#include <ArduinoJson.h>
#include <WebSocketsClient.h>

#include "Timer.h"

///
/// <summary>
/// Shared WebSocket/JSON plumbing for clients talking to the DeviceServer (TelemetryClient
/// and DeviceServerClient): dual-endpoint connect/failover, reconnect/heartbeat setup, and
/// JSON text-frame parsing/dispatch. Subclasses implement the handshake and message
/// handling specific to their protocol by overriding _onConnected(), _onDisconnected(),
/// and _onJsonMessage().
/// </summary>
/// <remarks>
/// An optional fallback endpoint lets a client alternate between two servers (e.g. the
/// local LAN server and a public server) until one works: if the primary endpoint isn't
/// ready (see isReady()/_ready) within FAILOVER_TIMEOUT_MS, loop() switches to the other
/// endpoint, and so on until one connects and the handshake is acknowledged.
/// </remarks>
///
class WebSocketJsonClient
{
private:
   std::string _hosts[2];
   uint16_t _ports[2] = { 0, 0 };
   bool _tls[2] = { false, false };
   std::string _path;
   uint8_t _numEndpoints = 0;
   bool _switching = false;

   void _connect()
   {
      const char* host = _hosts[_endpointIndex].c_str();
      if (_tls[_endpointIndex])
      {
         _webSocket.beginSSL(host, _ports[_endpointIndex], _path.c_str());
      }
      else
      {
         _webSocket.begin(host, _ports[_endpointIndex], _path.c_str());
      }
      _failoverTimer.reset();
   }

   void _onEvent(WStype_t type, uint8_t* payload, size_t length)
   {
      switch (type)
      {
         case WStype_CONNECTED:
            _connected = true;
            _hasConnected = true;
            _ready = false;
            _onConnected();
            break;

         case WStype_DISCONNECTED:
         {
            std::string reason = (payload != nullptr && length > 0) ? std::string((const char*)payload, length) : "";
            bool wasConnected = _hasConnected;

            _connected = false;
            _hasConnected = false;
            _ready = false;
            _failoverTimer.reset();

            if (!_switching)
            {
               _onDisconnected(reason, wasConnected);
            }
            break;
         }

         case WStype_ERROR:
         {
            std::string reason = (payload != nullptr && length > 0) ? std::string((const char*)payload, length) : "";
            _onError(reason);
            break;
         }

         case WStype_TEXT:
         {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload, length);
            if (error)
            {
               _onError(std::string("invalid message: ") + error.c_str());
            }
            else
            {
               _onJsonMessage(doc);
            }
            break;
         }

         default:
            break;
      }
   }

protected:
   static constexpr uint32_t FAILOVER_TIMEOUT_MS = 6000;
   static constexpr uint32_t RECONNECT_INTERVAL_MS = 2000;

   WebSocketsClient _webSocket;
   std::string _token;
   bool _connected = false;
   bool _hasConnected = false;
   bool _ready = false;
   uint8_t _endpointIndex = 0;
   uint8_t _switchCount = 0;
   TimerMillis _failoverTimer{ FAILOVER_TIMEOUT_MS };

   ///
   /// <summary>
   /// Number of endpoints configured (1 if no fallback was set via setFallbackEndpoint(), otherwise 2).
   /// </summary>
   ///
   uint8_t numEndpoints() const
   {
      return _numEndpoints;
   }

   ///
   /// <summary>
   /// Sends a JSON document as a WebSocket text frame.
   /// </summary>
   /// <param name="doc">The document to serialize and send.</param>
   ///
   void _sendJson(JsonDocument& doc)
   {
      std::string json;
      serializeJson(doc, json);
      _webSocket.sendTXT(json.c_str());
   }

   ///
   /// <summary>
   /// Invoked when the WebSocket connection is established; subclasses should send
   /// their handshake here.
   /// </summary>
   ///
   virtual void _onConnected() = 0;

   ///
   /// <summary>
   /// Invoked when the WebSocket connection is lost or a connection attempt fails
   /// (ignored while loop() is switching endpoints on purpose).
   /// </summary>
   /// <param name="reason">Low-level reason reported by the WebSocket client, if any.</param>
   /// <param name="wasConnected">True if the connection had previously been established (a real disconnect); false if this is the first attempt failing.</param>
   ///
   virtual void _onDisconnected(const std::string& reason, bool wasConnected) = 0;

   ///
   /// <summary>
   /// Invoked for each successfully parsed JSON message received.
   /// </summary>
   /// <param name="doc">The parsed JSON document.</param>
   ///
   virtual void _onJsonMessage(JsonDocument& doc) = 0;

   ///
   /// <summary>
   /// Invoked when the WebSocket reports a low-level error, or a received text frame
   /// fails to parse as JSON. Default implementation does nothing.
   /// </summary>
   /// <param name="reason">The error reason/message.</param>
   ///
   virtual void _onError(const std::string& reason)
   {
      (void)reason;
   }

   ///
   /// <summary>
   /// Invoked once per loop() call, after endpoint failover and the WebSocket's own
   /// loop() have run. Default implementation does nothing.
   /// </summary>
   ///
   virtual void _onLoop()
   {
   }

public:
   virtual ~WebSocketJsonClient() = default;

   ///
   /// <summary>
   /// Sets the authentication token sent in the handshake. Call before begin().
   /// </summary>
   /// <param name="token">Token issued for this client.</param>
   ///
   void setToken(const char* token)
   {
      _token = token;
   }

   ///
   /// <summary>
   /// Sets a fallback endpoint (e.g. a LAN server). If the primary endpoint isn't ready
   /// within a few seconds, loop() alternates between the two endpoints until one
   /// works. Call before begin().
   /// </summary>
   /// <param name="host">Fallback server host name or IP address.</param>
   /// <param name="port">Fallback server port.</param>
   /// <param name="useTls">True to connect with TLS (wss://).</param>
   ///
   void setFallbackEndpoint(const char* host, uint16_t port, bool useTls)
   {
      _hosts[1] = host;
      _ports[1] = port;
      _tls[1] = useTls;
   }

   ///
   /// <summary>
   /// Connects to the given endpoint. Connection completes asynchronously; call loop() regularly.
   /// </summary>
   /// <param name="host">Server host name or IP address.</param>
   /// <param name="port">Server port.</param>
   /// <param name="useTls">True to connect with TLS (wss://); false for plain ws://.</param>
   /// <param name="path">WebSocket path.</param>
   /// <param name="heartbeatPingMs">How often to ping the server to detect a dead/idle connection.</param>
   /// <param name="heartbeatTimeoutMs">How long to wait for a pong reply before counting it as missed.</param>
   /// <param name="heartbeatFailures">Number of consecutive missed pongs before the connection is torn down and reconnected.</param>
   ///
   void begin(const char* host, uint16_t port, bool useTls, const char* path, uint32_t heartbeatPingMs, uint32_t heartbeatTimeoutMs, uint8_t heartbeatFailures)
   {
      _path = path;
      _hosts[0] = host;
      _ports[0] = port;
      _tls[0] = useTls;
      _numEndpoints = (_ports[1] != 0) ? 2 : 1;
      _endpointIndex = 0;
      _connect();
      _webSocket.onEvent([this](WStype_t type, uint8_t* payload, size_t length)
      {
         _onEvent(type, payload, length);
      });
      _webSocket.setReconnectInterval(RECONNECT_INTERVAL_MS);
      _webSocket.enableHeartbeat(heartbeatPingMs, heartbeatTimeoutMs, heartbeatFailures);
   }

   ///
   /// <summary>
   /// Services the WebSocket and endpoint failover. Call every pass through loop().
   /// </summary>
   ///
   void loop()
   {
      if (_numEndpoints > 1 && !_ready && _failoverTimer.ready())
      {
         _endpointIndex = (_endpointIndex + 1) % _numEndpoints;
         if (_switchCount < 255)
         {
            _switchCount++;
         }
         _switching = true;
         _webSocket.disconnect();
         _switching = false;
         _connect();
      }

      _onLoop();

      // WebSocketsClient::loop() handles at most one incoming frame per call, so drain
      // the socket within a small time budget; otherwise a fast publisher (or a slow
      // main loop) builds an ever-growing backlog that delays pongs and live data.
      constexpr uint32_t DRAIN_BUDGET_MS = 10;
      constexpr uint8_t MAX_FRAMES_PER_LOOP = 50;
      const uint32_t start = millis();
      for (uint8_t i = 0; i < MAX_FRAMES_PER_LOOP; i++)
      {
         _webSocket.loop();
         if (millis() - start >= DRAIN_BUDGET_MS)
         {
            break;
         }
      }
   }

   ///
   /// <summary>
   /// Indicates whether every configured endpoint has had a full failover timeout without
   /// becoming ready (always true when only one endpoint is configured).
   /// </summary>
   /// <returns>True if all endpoints have been tried and failed; otherwise false.</returns>
   ///
   bool hasTriedAllEndpoints() const
   {
      return _numEndpoints <= 1 || _switchCount >= _numEndpoints;
   }

   ///
   /// <summary>
   /// Gets the URL of the endpoint currently being used.
   /// </summary>
   /// <returns>The server URL.</returns>
   ///
   std::string getUrl() const
   {
      return std::string(_tls[_endpointIndex] ? "wss://" : "ws://") + _hosts[_endpointIndex] + ":" + std::to_string(_ports[_endpointIndex]) + _path;
   }

   ///
   /// <summary>
   /// Gets the host name of the endpoint currently being used (no scheme, port or path).
   /// </summary>
   /// <returns>The server host name.</returns>
   ///
   std::string getHost() const
   {
      return _hosts[_endpointIndex];
   }

   ///
   /// <summary>
   /// Indicates whether the endpoint currently being used connects with TLS (wss://).
   /// </summary>
   /// <returns>True if the current endpoint uses TLS; otherwise false.</returns>
   ///
   bool isTls() const
   {
      return _tls[_endpointIndex];
   }

   ///
   /// <summary>
   /// Indicates whether the client is currently connected to the primary endpoint (the
   /// one passed to begin()) rather than the fallback endpoint set via
   /// setFallbackEndpoint() (e.g. a LAN server vs. the public Cloudflare server).
   /// </summary>
   /// <returns>True if connected to the primary (first) endpoint; false if using the fallback.</returns>
   ///
   bool isDirectConnection() const
   {
      return _endpointIndex == 0;
   }

   ///
   /// <summary>
   /// Returns whether the WebSocket is connected.
   /// </summary>
   /// <returns>True if connected; otherwise false.</returns>
   ///
   bool isConnected() const
   {
      return _connected;
   }

   ///
   /// <summary>
   /// Returns whether the server has acknowledged the handshake.
   /// </summary>
   /// <returns>True if ready to publish/receive/log; otherwise false.</returns>
   ///
   bool isReady() const
   {
      return _ready;
   }

   ///
   /// <summary>
   /// Sends a JSON document as a WebSocket text frame. Public counterpart of
   /// _sendJson(), for use by callers that hold a WebSocketJsonClient instance rather
   /// than being a subclass (e.g. a static facade delegating to an owned connection).
   /// </summary>
   /// <param name="doc">The document to serialize and send.</param>
   ///
   void sendJson(JsonDocument& doc)
   {
      _sendJson(doc);
   }

   ///
   /// <summary>
   /// Sends an already-serialized JSON string as a WebSocket text frame.
   /// </summary>
   /// <param name="json">The JSON text to send.</param>
   ///
   void sendRaw(const std::string& json)
   {
      _webSocket.sendTXT(json.c_str());
   }
};
