#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID,
// WIFI_PASSWORD, TELEMETRY_HOST, TELEMETRY_PORT, INFLUXDB_URL, and INFLUXDB_ORG are
// defined). This mirrors the include order already used by Gate/Wind/Wave_Publisher.

#include "InfluxSketchBase.h"
#include "TelemetryClient.h"
#include "TelemetryFeature.h"

///
/// <summary>
/// Owns the initialization and loop sequence shared by every Gate/Wind/Wave-style
/// publisher sketch: startup print, force-prompt window, sensor init, site resolution,
/// WiFi, rebooter, OTA, InfluxDB setup (including a single startup log point with the
/// sketch name, version, and telemetry topic), standard enclosure/CPU points, and the
/// telemetry WebSocket client. A sketch registers its sensors, published value, extra
/// Influx points, and per-loop work via the methods below before calling begin(), then
/// calls begin() once from setup() and loop() once from loop(). Shared lifecycle logic
/// lives in InfluxSketchBase; this class adds the telemetry-specific pieces on top.
/// </summary>
///
class PublisherSketch : public InfluxSketchBase
{
private:
   /// <summary>Function that produces the value streamed over telemetry.</summary>
   float (*_valueSource)() = nullptr;

   ///
   /// <summary>
   /// Default telemetry event handler used unless a custom handler is registered via
   /// setTelemetryHandler(). Extends TelemetryEventHandler with a single extra
   /// onStarted() callback slot (see PublisherSketch::setOnStartedCallback()) so a sketch can
   /// run its own onStarted() logic (e.g. turning off the status LED) without
   /// subclassing TelemetryEventHandler and risking skipping base-class behavior that
   /// Publisher itself may rely on.
   /// </summary>
   ///
   class PublisherTelemetryHandler : public TelemetryEventHandler
   {
   private:
      std::function<void()> _onStartedCallback = nullptr;

   public:
#ifdef ARDUINO_DISPLAY_SUPPORTED
      explicit PublisherTelemetryHandler(IStatus* status, ArduinoWithDisplay* display = nullptr) : TelemetryEventHandler(status, display)
      {
      }
#else
      explicit PublisherTelemetryHandler(IStatus* status) : TelemetryEventHandler(status)
      {
      }
#endif

      void setOnStartedCallback(std::function<void()> callback)
      {
         _onStartedCallback = callback;
      }

      void onStarted() override
      {
         TelemetryEventHandler::onStarted();

         if (_onStartedCallback != nullptr)
         {
            _onStartedCallback();
         }
      }
   };

   /// <summary>Default telemetry event handler driving the status LED on connect/disconnect, used unless a custom handler is registered via setTelemetryHandler().</summary>
   PublisherTelemetryHandler _telemetryHandler;

   /// <summary>Custom handler registered via setTelemetryHandler(), used instead of _telemetryHandler if set.</summary>
   TelemetryEventHandler* _customTelemetryHandler = nullptr;

   /// <summary>Telemetry settings, copied from the constructor argument.</summary>
   TelemetryConfig _telemetryConfig;

   /// <summary>Resolves the telemetry topic and connects the telemetry client.</summary>
   TelemetryFeature _telemetryFeature;

   /// <summary>Constructed by begin(), once the telemetry topic has been resolved.</summary>
   TelemetryPublisher* _client = nullptr;

   Timer _publishTimer;

protected:
   ///
   /// <summary>
   /// Publisher's fixed-site fallback (used when influxConfig.prompts is empty): an empty
   /// InfluxContext, since Publisher has no fixed Influx site of its own.
   /// </summary>
   ///
   InfluxContext _resolveFixedSite() override
   {
      return {};
   }

   ///
   /// <summary>
   /// Returns whether the telemetry topic is selected from a prompt table.
   /// </summary>
   /// <returns>True if telemetryConfig.prompts is non-empty.</returns>
   ///
   bool _hasExtraPrompts() override
   {
      return _telemetryFeature.hasPrompts();
   }

   ///
   /// <summary>
   /// Resolves the telemetry topic and reports it.
   /// </summary>
   /// <param name="forcePrompt">If true, always prompts even if a saved topic exists.</param>
   ///
   void _resolveExtra(bool forcePrompt) override
   {
      std::string telemetryMessage = std::string("Telemetry topic: ") + _telemetryFeature.resolve(_arduino, _status, forcePrompt);
      _printAndLogStatus(telemetryMessage.c_str());
   }

   ///
   /// <summary>
   /// Adds the telemetry topic to a GetStatus reply, then defers to
   /// InfluxSketchBase::_populateStatus().
   /// </summary>
   /// <param name="status">The in-progress status to add fields to.</param>
   ///
   void _populateStatus(LoggerStatus& status) override
   {
      _telemetryFeature.addStatus(status);

      InfluxSketchBase::_populateStatus(status);
   }

   ///
   /// <summary>Publisher only uses Influx when a selectable site table resolved a bucket.</summary>
   ///
   bool _shouldUseInflux(bool hasSiteTable) override
   {
      return hasSiteTable;
   }

   ///
   /// <summary>
   /// Constructs the telemetry WebSocket client and starts its connection, then blocks
   /// (via ArduinoBase::waitForClient()) until it resolves (connects or fails) before
   /// returning, so its "Telemetry... " label always completes before the next setup
   /// step (Logger.begin()) can print anything. The telemetry client itself remains
   /// fully async - only this setup-time wait is blocking - and Influx::endInit() is not
   /// called since the initialization display (WiFi, Time, Influx, and now Telemetry
   /// rows) is left on-screen so the connection's OK/FAILED result (printed by
   /// TelemetryEventHandler) stays visible. Sketches that show a different UI once
   /// connected (e.g. via client()->isStarted()) are responsible for clearing the
   /// display themselves at that point.
   /// </summary>
   ///
   void _afterOTASetup() override
   {
      _client = new TelemetryPublisher(_telemetryFeature.topic(), _telemetryConfig.decimals, _status, _customTelemetryHandler != nullptr ? _customTelemetryHandler : &_telemetryHandler);
      _telemetryFeature.connect(_arduino, _status, _client);
   }

   ///
   /// <summary>Publishes the current value (if due) and polls the telemetry client, before the standard OTA/sensor/Influx loop work runs.</summary>
   ///
   void _beforeOTACheck() override
   {
      if (_client->isStarted())
      {
         // without a delay, the waveshare crashes
         delay(1);

         if (_publishTimer.ready() && _valueSource != nullptr)
         {
            _client->setValue(_valueSource());
         }
      }

      _client->loop(); // Continuously poll for events and maintain connection
   }

   ///
   /// <summary>Publisher only runs the standard Influx post/flush cycle once telemetry has started.</summary>
   ///
   bool _extraInfluxReadyCondition() override
   {
      return _client->isStarted();
   }

   ///
   /// <summary>Publisher posts points asynchronously.</summary>
   ///
   bool _postAsync() override
   {
      return true;
   }

public:
   ///
   /// <summary>
   /// Creates a PublisherSketch bound to the given board and configuration. Register
   /// sensors, the value source, extra Influx points, and loop hooks afterward, then
   /// call begin().
   /// </summary>
   /// <param name="arduino">The board wrapper (used as the status indicator directly if it implements IStatus itself; otherwise its onboard NeoPixel LED is used).</param>
   /// <param name="config">Shared configuration.</param>
   /// <param name="influxConfig">InfluxDB settings.</param>
   /// <param name="telemetryConfig">Telemetry settings (topic table or fixed topic, decimals, publish cadence).</param>
   ///
   PublisherSketch(Arduino* arduino, const SketchConfig& config, const InfluxConfig& influxConfig, const TelemetryConfig& telemetryConfig)
      : InfluxSketchBase(arduino, config, influxConfig),
#ifdef ARDUINO_DISPLAY_SUPPORTED
        _telemetryHandler(_status, arduino),
#else
        _telemetryHandler(_status),
#endif
        _telemetryConfig(telemetryConfig),
        _telemetryFeature(config.preferencesNamespace, &_telemetryConfig),
        _publishTimer(telemetryConfig.publishIntervalMs)
   {
   }

   ///
   /// <summary>
   /// Registers the function that produces the value streamed over telemetry at the
   /// configured publish cadence. If the underlying sensor needs to be read/refreshed
   /// first (e.g. calling read() on a driver before x()/y()/etc. return updated values),
   /// do that inside valueFunc before returning the value.
   /// </summary>
   /// <param name="valueFunc">Captureless function returning the value to publish.</param>
   ///
   void setValueSource(float (*valueFunc)())
   {
      _valueSource = valueFunc;
   }

   ///
   /// <summary>
   /// Registers a custom telemetry event handler (e.g. to drive custom on-screen status
   /// text/colors), used instead of the default TelemetryEventHandler. Must be called
   /// before begin(), since begin() constructs the telemetry client with whichever
   /// handler is active at that point.
   /// </summary>
   /// <param name="handler">Custom handler instance, owned by the caller.</param>
   ///
   void setTelemetryHandler(TelemetryEventHandler* handler)
   {
      _customTelemetryHandler = handler;
   }

   ///
   /// <summary>
   /// Registers a callback invoked when telemetry finishes starting, run after the
   /// default TelemetryEventHandler::onStarted() behavior (status set to READY, etc.).
   /// Useful for sketch-specific startup completion behavior (e.g. turning off the
   /// status LED) without the risk of subclassing TelemetryEventHandler and missing
   /// base-class behavior PublisherSketch relies on. Ignored if a custom handler is
   /// registered via setTelemetryHandler(), since that replaces this default handler
   /// entirely. Must be called before begin().
   /// </summary>
   /// <param name="callback">Function invoked once telemetry finishes starting.</param>
   ///
   void setOnStartedCallback(std::function<void()> callback)
   {
      _telemetryHandler.setOnStartedCallback(callback);
   }

   ///
   /// <summary>Returns the telemetry client, constructed by begin().</summary>
   ///
   TelemetryPublisher* client()
   {
      return _client;
   }

   ///
   /// <summary>Returns the resolved telemetry topic (only valid after begin() returns).</summary>
   ///
   const char* telemetryTopic() const
   {
      return _telemetryFeature.topic();
   }
};
