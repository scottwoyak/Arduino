#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ArduinoJson.h>
#include <WebSocketsClient.h>

#include "DeviceHealth.h"
#include "RollingRate.h"
#include "Status.h"
#include "Stopwatch.h"
#include "TelemetryConfig.h"
#include "Timer.h"
#include "Util.h"
#include "Logger.h"
#include "WebSocketJsonClient.h"

// Display-based error rendering is only available on boards with a display.
// ARDUINO_DISPLAY_SUPPORTED is defined by ArduinoBoard.h when the target board has one.
#ifdef ARDUINO_DISPLAY_SUPPORTED
#include "ArduinoWithDisplay.h"
#endif

///
/// <summary>
/// Number of seconds to wait before resetting the device after a telemetry error or
/// disconnect, giving Serial/display output time to be seen.
/// </summary>
///
constexpr float TELEMETRY_RESET_DELAY_S = 10.0f;

///
/// <summary>
/// Number of minutes the telemetry connection may stay down (dropped or never connected)
/// while the WebSocket retries in the background before the outage is reported to the
/// event handler, which by default resets the device.
/// </summary>
///
constexpr float TELEMETRY_OUTAGE_RESET_M = 10.0f;

///
/// <summary>
/// Number of samples used by TelemetryClient's rolling message rate tracker.
/// </summary>
///
constexpr uint16_t TELEMETRY_RATE_NUM_SAMPLES = 50;

///
/// <summary>
/// Throttles logging of telemetry reconnect failures (disconnects, failed connection
/// attempts, errors) so a persistent outage doesn't flood the log with one message per
/// retry attempt - the underlying WebSocket keeps retrying in the background, often
/// multiple times per minute. The first failure since the last successful connection is
/// logged immediately with a caller-supplied detail message; further failures are
/// suppressed until the configured interval has elapsed, at which point a summary is
/// logged reporting the total elapsed downtime so far, and so on for as long as the
/// outage continues. Call reportSuccess() once the topic starts working again so the
/// next failure is logged immediately. Useful both for TelemetryEventHandler overrides
/// and for minimal/custom WebSocket clients (e.g. a sketch subscribing to a topic the
/// telemetry server can't yet multiplex onto an existing TelemetrySubscriber's
/// connection).
/// </summary>
///
class ReconnectLogThrottle
{
private:
   std::string _topic;
   bool _failureActive = false;
   TimerSecs _logTimer;
   Stopwatch _outageStopwatch;

public:
   ///
   /// <summary>
   /// Initializes a new instance of the ReconnectLogThrottle class.
   /// </summary>
   /// <param name="topic">Telemetry topic name, used in the throttled "still down" summary message.</param>
   /// <param name="intervalSecs">Minimum time between "still down" summary log messages, in seconds.</param>
   ///
   ReconnectLogThrottle(const std::string& topic, float intervalSecs)
      : _topic(topic), _logTimer(intervalSecs)
   {
   }

   ///
   /// <summary>
   /// Logs a reconnect-related failure, throttled as described in the class summary.
   /// </summary>
   /// <param name="detail">Detail message to log for the first failure in a new outage.</param>
   ///
   void reportFailure(const std::string& detail)
   {
      if (!_failureActive)
      {
         _failureActive = true;
         _logTimer.reset();
         _outageStopwatch.reset();
         _outageStopwatch.start();
         Logger.log(detail, LogSeverity::ERROR);
      }
      else if (_logTimer.ready())
      {
         _logTimer.reset();
         uint16_t elapsedMinutes = (uint16_t)lround(_outageStopwatch.elapsedSecs() / 60.0);
         Logger.log("Haven't been able to reconnect to telemetry topic '" + _topic + "' for the last " + std::to_string(elapsedMinutes) + " minutes", LogSeverity::ERROR);
      }
   }

   ///
   /// <summary>
   /// Marks the topic as healthy again, so the next failure is logged immediately
   /// instead of being throttled.
   /// </summary>
   ///
   void reportSuccess()
   {
      _failureActive = false;
      _outageStopwatch.stop();
   }
};

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
   /// sets the status to RUNNING. The "Telemetry..." label printed by
   /// ArduinoBase::initClient() is completed by TelemetryClient::connect(), so this
   /// method does not print anything. Overrides
   /// </summary>
   ///
   virtual void onStarted()
   {
      _status->setStatus(Status::RUNNING);
   }

   ///
   /// <summary>
   /// Invoked when the telemetry connection has been down for TELEMETRY_OUTAGE_RESET_M
   /// minutes after being lost (the client retries in the background until then).
   /// Default implementation logs the reason,
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
      Util::reset(TELEMETRY_RESET_DELAY_S, std::string("Telemetry connection lost (") + reason + ")");
   }

   ///
   /// <summary>
   /// Invoked when the WebSocket has never successfully connected for
   /// TELEMETRY_OUTAGE_RESET_M minutes (retried in the background until then; e.g. the
   /// server isn't running, or it's unreachable). Default
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
      Util::reset(TELEMETRY_RESET_DELAY_S, std::string("Could not connect to telemetry server (") + reason + ")");
   }

   ///
   /// <summary>
   /// Invoked when the telemetry client reports an error. Default implementation logs
   /// the message, draws it on the display (if one was supplied), and sets the status to
   /// FAILED. Does not reset the device.
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
/// Client for the TelemetryServer (WebSocket at /ws, optionally TLS).
/// </summary>
/// <remarks>
/// A client is either a publisher (DEVICE role) or a subscriber (CLIENT role).
/// Protocol (JSON text frames):
///   Handshake, sent on connect: {"role":"device","token":"..."} or {"role":"client","token":"...","topics":["A","B"]}
///   Server reply:               {"type":"ack",...}
///   Publish (device to server): {"topic":"Test","value":1.23,"dt":100}
///   Sample (server to client):  {"topic":"Test","value":1.23,"dt":100}
/// Samples are only published once the server has acknowledged the handshake.
/// The "dt" field is the number of microseconds since this client's previous publish.
/// An optional fallback endpoint lets the client alternate between two servers (e.g. the
/// public Cloudflare host and a LAN server) until one works.
/// </remarks>
///
class TelemetryClient : public WebSocketJsonClient
{
public:
   enum class Role
   {
      DEVICE,
      CLIENT,
   };

   typedef std::function<void(const std::string& topic, double value, int64_t dtMicros)> SampleHandler;

private:
   std::string _topic;
   TelemetryConfig _config;
   Role _role = Role::DEVICE;
   std::vector<std::string> _topics;
   SampleHandler _sampleHandler = nullptr;
   float _value = NAN;
   RollingRate _rate;
   struct PublishTime
   {
      std::string topic;
      uint32_t micros;
   };
   std::vector<PublishTime> _publishTimes;

   // user event handler; owned by this instance only when created by the constructor
   TelemetryEventHandler* _handler = nullptr;
   bool _ownsHandler = false;

   // set while an established connection has dropped and we are waiting for it to return
   bool _dropActive = false;
   bool _dropWasConnected = false;
   std::string _dropReason;
   Stopwatch _dropStopwatch;

   static constexpr uint32_t HEARTBEAT_PING_MS = 15000;
   static constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 3000;
   static constexpr uint8_t HEARTBEAT_FAILURES = 2;

   void _sendHandshake()
   {
      JsonDocument doc;
      doc["role"] = (_role == Role::DEVICE) ? "device" : "client";
      if (!_token.empty())
      {
         doc["token"] = _token;
      }
      if (_role == Role::CLIENT)
      {
         JsonArray topics = doc["topics"].to<JsonArray>();
         for (const std::string& topic : _topics)
         {
            topics.add(topic);
         }
      }

      _sendJson(doc);
   }

   void _onConnected() override
   {
      _sendHandshake();
      if (_handler != nullptr)
      {
         _handler->onConnected();
      }
   }

   void _onDisconnected(const std::string& reason, bool wasConnected) override
   {
      _onTelemetryDisconnected();
      if (wasConnected)
      {
         DeviceHealth::telemetryReconnects++;
      }

      if (_dropActive)
      {
         return;
      }

      _dropActive = true;
      _dropWasConnected = wasConnected;
      _dropReason = reason;
      _dropStopwatch.reset();
      _dropStopwatch.start();
      Logger.log(std::string(wasConnected ? "Telemetry connection lost (" : "Could not connect to telemetry server (") + reason + "); will keep retrying for up to " + std::to_string((int)TELEMETRY_OUTAGE_RESET_M) + " minutes", LogSeverity::WARN);
   }

   void _onJsonMessage(JsonDocument& doc) override
   {
      const char* type = doc["type"];
      if (type != nullptr)
      {
         if (strcmp(type, "ack") == 0)
         {
            _ready = true;
            _rate.reset();
            if (_dropActive)
            {
               _dropActive = false;
               _dropStopwatch.stop();
               Logger.log("Telemetry reconnected");
            }
            if (_handler != nullptr)
            {
               _handler->onStarted();
            }
         }
         else if (strcmp(type, "error") == 0)
         {
            const char* message = doc["message"] | "unknown error";
            if (_handler != nullptr)
            {
               _handler->onError(std::string("Telemetry: server error: ") + message);
            }
            else
            {
               Logger.log(std::string("Telemetry: server error: ") + message, LogSeverity::ERROR);
            }
         }
         return;
      }

      const char* topic = doc["topic"];
      if (topic == nullptr || !doc["value"].is<double>())
      {
         return;
      }

      double value = doc["value"].as<double>();
      _value = (float)value;
      _rate.tick();
      DeviceHealth::telemetryRate = _rate.get();
      if (_sampleHandler != nullptr)
      {
         _sampleHandler(topic, value, doc["dt"] | 0);
      }
      if (_handler != nullptr)
      {
         _handler->onReceiveText(std::to_string(value));
      }
   }

   void _onError(const std::string& reason) override
   {
      Logger.log(std::string("Telemetry: invalid message: ") + reason, LogSeverity::ERROR);
   }

protected:
    void _onLoop() override
    {
       if (_dropActive && _dropStopwatch.elapsedSecs() >= TELEMETRY_OUTAGE_RESET_M * 60.0f)
       {
          _dropActive = false;
          _dropStopwatch.stop();
          Logger.log("Telemetry did not reconnect within " + std::to_string((int)TELEMETRY_OUTAGE_RESET_M) + " minutes", LogSeverity::ERROR);
          if (_handler != nullptr)
          {
             if (_dropWasConnected)
             {
                _handler->onDisconnected(_dropReason);
             }
             else
             {
                _handler->onConnectionFailed(_dropReason);
             }
          }
       }
    }

    ///
    /// <summary>
    /// Called when the WebSocket connection is lost or a connection attempt fails.
   /// Subclasses should reset any per-connection state here.
   /// </summary>
   ///
   virtual void _onTelemetryDisconnected()
   {
   }

   ///
   /// <summary>
   /// Sets the role and topic used in the handshake.
   /// </summary>
   /// <param name="role">DEVICE to publish, CLIENT to subscribe</param>
   /// <param name="topic">Topic name</param>
   ///
   void _setRole(Role role, const std::string& topic)
   {
      ASSERT(!topic.empty());

      _role = role;
      _topic = topic;
      _topics = { topic };
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the TelemetryClient class.
   /// </summary>
   /// <param name="config">Telemetry settings; the server endpoints, path and tokens are used by begin().</param>
   /// <param name="status">Status indicator used by the default event handler. If nullptr and no handler is supplied, no handler is used.</param>
   /// <param name="handler">Event handler for connection lifecycle events, or nullptr to use a default handler.</param>
   ///
   explicit TelemetryClient(const TelemetryConfig& config, IStatus* status = nullptr, TelemetryEventHandler* handler = nullptr)
      : _config(config),
        _rate(TELEMETRY_RATE_NUM_SAMPLES)
   {
      if (handler != nullptr)
      {
         _handler = handler;
      }
      else if (status != nullptr)
      {
         _handler = new TelemetryEventHandler(status);
         _ownsHandler = true;
      }
   }

   virtual ~TelemetryClient()
   {
      if (_ownsHandler)
      {
         delete _handler;
      }
   }

   ///
   /// <summary>
   /// Replaces the event handler used by this client. Any previously owned default
   /// handler is deleted.
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
   /// Connects using the role and topic already configured (see TelemetryPublisher and
   /// TelemetrySubscriber). Connection completes asynchronously; call loop() regularly.
   /// </summary>
   /// <param name="host">Server host name or IP address</param>
   /// <param name="port">Server port</param>
   /// <param name="useTls">True to connect with TLS (wss://); false for plain ws://</param>
   /// <param name="path">WebSocket path</param>
   ///
   void begin(const char* host, uint16_t port, bool useTls = false, const char* path = "/ws")
   {
      WebSocketJsonClient::begin(host, port, useTls, path, HEARTBEAT_PING_MS, HEARTBEAT_TIMEOUT_MS, HEARTBEAT_FAILURES);
   }

   ///
   /// <summary>
   /// Connects using the given server configuration: sets the token matching this
   /// client's role and the fallback endpoint (if any), then connects to the primary
   /// endpoint. Connection completes asynchronously; call loop() regularly.
   /// </summary>
   ///
   void begin()
   {
      ASSERT(_config.primary.host != nullptr);

      setToken(_role == Role::DEVICE ? _config.deviceToken : _config.clientToken);
      if (_config.fallback.port != 0)
      {
         setFallbackEndpoint(_config.fallback.host, _config.fallback.port, _config.fallback.useTls);
      }
      begin(_config.primary.host, _config.primary.port, _config.primary.useTls, _config.path);
   }

   ///
   /// <summary>
   /// Starts this client's connection, printing the standard "Telemetry..." init status
   /// line, and blocks until it resolves (connects or fails). The client itself remains
   /// async afterward. If it hasn't started within the connect timeout, "FAILED" is
   /// reported but the client is left running so it keeps retrying in the background;
   /// the caller decides how to proceed. Requires ArduinoBoard.h to have been included.
   /// </summary>
   /// <param name="arduino">The board wrapper.</param>
   /// <param name="status">Status indicator driven during the connection.</param>
   /// <returns>True if the client started before the timeout; false otherwise.</returns>
   ///
   bool connect(Arduino* arduino, IStatus* status)
   {
      arduino->initClient("Telemetry", [this]() { begin(); }, status);
      if (arduino->waitForClient([this]() { return isStarted(); }, [this]() { loop(); }))
      {
         // "Direct" vs "OK" lets you tell at a glance (via serial/display) whether the
         // connection went straight to the local/LAN server or had to fall back to the
         // public (e.g. Cloudflare) endpoint - see TelemetryClient::isDirectConnection().
         std::string result = isDirectConnection() ? getHost() : "OK";
         Logger.log(result);
         arduino->printlnR(result.c_str(), Color::VALUE);
         return true;
      }

      status->setStatus(Status::FAILED);
      Logger.log("FAILED", LogSeverity::ERROR);
      arduino->printlnR("FAILED", Color::RED);
      return false;
   }

   ///
   /// <summary>
   /// Connects as a publisher.
   /// </summary>
   /// <param name="host">Server host name or IP address</param>
   /// <param name="port">Server port</param>
   /// <param name="useTls">True to connect with TLS (wss://); false for plain ws://</param>
   /// <param name="path">WebSocket path</param>
   ///
   void beginPublisher(const char* host, uint16_t port, bool useTls = false, const char* path = "/ws")
   {
      _role = Role::DEVICE;
      begin(host, port, useTls, path);
   }

   ///
   /// <summary>
   /// Connects as a subscriber to the given topics. Connection completes asynchronously;
   /// call loop() regularly.
   /// </summary>
   /// <param name="host">Server host name or IP address</param>
   /// <param name="port">Server port</param>
   /// <param name="topics">Topics to subscribe to. Empty subscribes to all topics.</param>
   /// <param name="useTls">True to connect with TLS (wss://); false for plain ws://</param>
   /// <param name="path">WebSocket path</param>
   ///
   void beginSubscriber(const char* host, uint16_t port, const std::vector<std::string>& topics, bool useTls = false, const char* path = "/ws")
   {
      _role = Role::CLIENT;
      _topics = topics;
      begin(host, port, useTls, path);
   }

   ///
   /// <summary>
   /// Sets the handler invoked for each received sample (subscribers only).
   /// </summary>
   /// <param name="handler">Handler called with the topic, value, and dt in microseconds</param>
   ///
   void onSample(SampleHandler handler)
   {
      _sampleHandler = handler;
   }

   ///
   /// <summary>
   /// Changes the subscribed topics on an existing subscriber connection.
   /// </summary>
   /// <param name="topics">New topics. Empty subscribes to all topics.</param>
   ///
   void setTopics(const std::vector<std::string>& topics)
   {
      _topics = topics;
      if (isConnected())
      {
         _sendHandshake();
      }
   }

   ///
   /// <summary>
   /// Publishes a sample (publishers only). Dropped if the connection isn't ready.
   /// </summary>
   /// <param name="topic">Topic name</param>
   /// <param name="value">Sample value</param>
   /// <param name="decimals">Number of decimal places for the value, or -1 to omit it from the message</param>
   /// <returns>True if the sample was sent; otherwise false.</returns>
   ///
   bool publish(const char* topic, double value, int8_t decimals = -1)
   {
      if (!isReady())
      {
         return false;
      }

      uint32_t now = micros();

      PublishTime* last = nullptr;
      for (PublishTime& publishTime : _publishTimes)
      {
         if (publishTime.topic == topic)
         {
            last = &publishTime;
            break;
         }
      }
      uint32_t dtMicros = last != nullptr ? (now - last->micros) : 0;

      JsonDocument doc;
      doc["topic"] = topic;
      doc["value"] = value;
      doc["dt"] = dtMicros;
      if (decimals >= 0)
      {
         doc["decimals"] = decimals;
      }

      std::string json;
      serializeJson(doc, json);
      if (!_webSocket.sendTXT(json.c_str()))
      {
         return false;
      }

      if (last != nullptr)
      {
         last->micros = now;
      }
      else
      {
         _publishTimes.push_back({ topic, now });
      }
      _rate.tick();
      return true;
   }

   ///
   /// <summary>
   /// Gets the role of this client.
   /// </summary>
   /// <returns>DEVICE for publishers; CLIENT for subscribers</returns>
   ///
   Role getRole() const
   {
      return _role;
   }

   void setTopic(const std::string& topic)
   {
      _topic = topic;
      _topics = { topic };
   }

   ///
   /// <summary>
   /// Gets the topic this client publishes or subscribes to (set by TelemetryPublisher / TelemetrySubscriber).
   /// </summary>
   /// <returns>The topic name, or empty if none was set.</returns>
   ///
   const std::string& getTopic() const
   {
      return _topic;
   }

   ///
   /// <summary>
   /// Gets the most recently received value (subscribers only).
   /// </summary>
   /// <returns>The latest value, or NAN if none has been received yet.</returns>
   ///
   float getValue() const
   {
      return _value;
   }

   ///
   /// <summary>
   /// Same as isReady().
   /// </summary>
   /// <returns>True if the handshake has been acknowledged; otherwise false.</returns>
   ///
   bool isStarted() const
   {
      return isReady();
   }

   ///
   /// <summary>
   /// Returns the rolling rate of published (or received) samples.
   /// </summary>
   /// <returns>Samples per second</returns>
   ///
   float getRate() const
   {
      return _rate.get();
   }
};

///
/// <summary>
/// TelemetryClient specialization that publishes a value to a topic, sending only when
/// the value (at the configured decimal places) changes.
/// </summary>
///
class TelemetryPublisher : public TelemetryClient
{
private:
   uint8_t _decimalPlaces;
   float _pendingValue = NAN;
   bool _pendingNew = false;
   bool _sendEverySample = false;
   std::string _lastValue = "";

   void _onTelemetryDisconnected() override
   {
      _lastValue = "";
   }

   void _onLoop() override
   {
      TelemetryClient::_onLoop();

      if (!isReady() || isnan(_pendingValue))
      {
         return;
      }

      String value(_pendingValue, (unsigned int)_decimalPlaces);
      bool changed = value != _lastValue.c_str();
      if ((changed || (_sendEverySample && _pendingNew)) && publish(getTopic().c_str(), value.toDouble(), _decimalPlaces))
      {
         _lastValue = value.c_str();
      }

      _pendingNew = false;
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the TelemetryPublisher class.
   /// </summary>
   /// <param name="topic">The telemetry topic to publish to.</param>
   /// <param name="config">Telemetry settings; decimals is the number of decimal places to publish values with.</param>
   /// <param name="status">Status indicator used by the default event handler, if no handler is supplied.</param>
   /// <param name="handler">Event handler for connection lifecycle events, or nullptr to use a default handler.</param>
   ///
   TelemetryPublisher(const std::string& topic, const TelemetryConfig& config, IStatus* status = nullptr, TelemetryEventHandler* handler = nullptr)
      : TelemetryClient(config, status, handler), _decimalPlaces(config.decimals)
   {
      _setRole(Role::DEVICE, topic);
   }

   ///
   /// <summary>
   /// Initializes a new instance of the TelemetryPublisher class that publishes to config.topic.
   /// </summary>
   /// <param name="config">Telemetry settings, with topic set.</param>
   /// <param name="status">Status indicator used by the default event handler, if no handler is supplied.</param>
   /// <param name="handler">Event handler for connection lifecycle events, or nullptr to use a default handler.</param>
   ///
   explicit TelemetryPublisher(const TelemetryConfig& config, IStatus* status = nullptr, TelemetryEventHandler* handler = nullptr)
      : TelemetryPublisher(config.topic, config, status, handler)
   {
   }

   ///
   /// <summary>
   /// Sets the value to be published on the next loop() call, if it has changed.
   /// </summary>
   /// <param name="value">The value to publish.</param>
   ///
   void setValue(float value)
   {
      _pendingValue = value;
      _pendingNew = true;
   }

   ///
   /// <summary>
   /// Sets whether every value passed to setValue() is published, even if it is unchanged
   /// at the configured decimal places. When false, only changed values are published.
   /// </summary>
   /// <param name="enable">True to publish every sample.</param>
   ///
   void setSendEverySample(bool enable)
   {
      _sendEverySample = enable;
   }
};

///
/// <summary>
/// TelemetryClient specialization that subscribes to a single topic.
/// </summary>
///
class TelemetrySubscriber : public TelemetryClient
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the TelemetrySubscriber class.
   /// </summary>
   /// <param name="topic">The telemetry topic to subscribe to.</param>
   /// <param name="status">Status indicator used by the default event handler, if no handler is supplied.</param>
   /// <param name="handler">Event handler for connection lifecycle events, or nullptr to use a default handler.</param>
   ///
   TelemetrySubscriber(const std::string& topic, const TelemetryConfig& config, IStatus* status = nullptr, TelemetryEventHandler* handler = nullptr)
      : TelemetryClient(config, status, handler)
   {
      _setRole(Role::CLIENT, topic);
   }

   ///
   /// <summary>
   /// Initializes a new instance of the TelemetrySubscriber class that subscribes to config.topic.
   /// </summary>
   /// <param name="config">Telemetry settings, with topic set.</param>
   /// <param name="status">Status indicator used by the default event handler, if no handler is supplied.</param>
   /// <param name="handler">Event handler for connection lifecycle events, or nullptr to use a default handler.</param>
   ///
   explicit TelemetrySubscriber(const TelemetryConfig& config, IStatus* status = nullptr, TelemetryEventHandler* handler = nullptr)
      : TelemetrySubscriber(config.topic, config, status, handler)
   {
   }
};
