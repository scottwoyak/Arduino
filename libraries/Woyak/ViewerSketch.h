#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID and
// WIFI_PASSWORD are defined). This mirrors the include order used by MonitorSketch-/
// PublisherSketch-based sketches (see InfluxSketchBase.h).

#include <functional>
#include <string>

#include "ArduinoBase.h"
#include "Logger.h"
#include "OTAUpdater.h"
#include "SketchBase.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "TelemetryConfig.h"
#include "TelemetryFeature.h"

///
/// <summary>
/// Owns the boot/init sequence shared by every display-only "Viewer" sketch (e.g.
/// Gate_Viewer, Wind_Viewer): printing the sketch name/version startup info to Serial
/// and the display, connecting to WiFi, and (optionally) enabling OTA firmware updates.
/// Unlike MonitorSketch/PublisherSketch (see InfluxSketchBase), a Viewer doesn't post to
/// InfluxDB or track a site/location; it only renders telemetry it receives. A sketch
/// constructs a ViewerSketch, calls begin() once from setup() (after registering its own
/// telemetry client(s) and before any sketch-specific WiFi-dependent setup), and calls
/// loop() once from loop().
/// </summary>
///
class ViewerSketch : public SketchBase
{
protected:
   /// <summary>Telemetry settings, copied from the constructor argument.</summary>
   TelemetryConfig _telemetryConfig;

   /// <summary>Resolves the telemetry topic (from telemetryConfig) and connects the telemetry client.</summary>
   TelemetryFeature _telemetryFeature;

   /// <summary>Constructed by beginTelemetry(), once the telemetry topic has been resolved.</summary>
   TelemetrySubscriber* _client = nullptr;

   ///
   /// <summary>
   /// Adds the telemetry topic to a GetStatus reply, then defers to
   /// SketchBase::_populateStatus().
   /// </summary>
   /// <param name="status">The in-progress status to add fields to.</param>
   ///
   void _populateStatus(LoggerStatus& status) override
   {
      _telemetryFeature.addStatus(status);

      SketchBase::_populateStatus(status);
   }

public:
   ///
   /// <summary>
   /// Creates a ViewerSketch bound to the given board and configuration.
   /// </summary>
   /// <param name="arduino">The board wrapper.</param>
   /// <param name="config">Shared configuration; preferencesNamespace is used by resolveTopic().</param>
   /// <param name="telemetryConfig">Telemetry settings (topic table or fixed topic) used by resolveTopic(). Defaults to empty for a viewer that passes its topic directly to beginTelemetry(topic, handler).</param>
   ///
   ViewerSketch(Arduino* arduino, const SketchConfig& config, const TelemetryConfig& telemetryConfig = {})
      : SketchBase(arduino, config),
        _telemetryConfig(telemetryConfig),
        _telemetryFeature(config.preferencesNamespace, &_telemetryConfig)
   {
   }

   ///
   /// <summary>
   /// Prints the sketch name/version startup info to Serial and the display. Call once
   /// from setup(), before any sketch-specific prompting (e.g. resolveTopic()) that must
   /// happen before WiFi connects, followed by beginConnect().
   /// </summary>
   ///
   void beginBanner()
   {
      _printStartupInfo();
   }

   ///
   /// <summary>
   /// Connects to WiFi, enables OTA if configured, and starts the Logger connection. Call
   /// once from setup(), after beginBanner() and any prompting that must happen before
   /// WiFi connects (e.g. resolveTopic()), and before registering any telemetry client
   /// handlers.
   /// </summary>
   ///
   void beginConnect()
   {
      _beginConnect();
      _beginLogger();
   }

   ///
   /// <summary>
   /// Runs the standard boot sequence for a Viewer with a fixed telemetry topic (no
   /// resolveTopic() prompting needed): beginBanner() followed immediately by
   /// beginConnect(). Call once from setup(), after registering any telemetry client
   /// handlers so they're ready to start connecting once WiFi is up.
   /// </summary>
   ///
   void begin()
   {
      beginBanner();
      beginConnect();
   }

   void loop()
   {
      _loopStep();

      if (_client != nullptr)
      {
         _client->loop();
      }
   }

   ///
   /// <summary>
   /// Resolves which telemetry topic this viewer should subscribe to, from
   /// telemetryConfig (a fixed topic, or a prompts table persisted in Preferences under
   /// config.preferencesNamespace). Also reports the resolved topic as an init status
   /// line. Call once from setup(), after beginBanner() and before beginConnect().
   /// </summary>
   /// <param name="forcePrompt">If true, always prompts even if a saved topic exists.</param>
   /// <returns>The resolved topic, backed by this ViewerSketch's storage.</returns>
   ///
   const char* resolveTopic(bool forcePrompt = false)
   {
      const char* topic = _telemetryFeature.resolve(_arduino, _status, forcePrompt);
      _printAndLogStatus("Topic... ", topic);
      return topic;
   }

   ///
   /// <summary>
   /// Constructs the TelemetrySubscriber for the topic resolved by resolveTopic() and
   /// connects it. Call once from setup(), after beginConnect() and any layout setup
   /// that depends on the topic.
   /// </summary>
   /// <param name="handler">Event handler for connection lifecycle events, or nullptr to use a default handler.</param>
   /// <returns>The constructed telemetry client, owned by this ViewerSketch.</returns>
   ///
   TelemetrySubscriber* beginTelemetry(TelemetryEventHandler* handler = nullptr)
   {
      ASSERT(_telemetryFeature.topic() != nullptr);

      return beginTelemetry(_telemetryFeature.topic(), handler);
   }

   ///
   /// <summary>
   /// Constructs the TelemetrySubscriber for the given topic and connects it, printing
   /// the standard "Telemetry..." init status line. Use for a viewer whose topic isn't
   /// resolved via resolveTopic().
   /// </summary>
   /// <param name="topic">The telemetry topic to subscribe to.</param>
   /// <param name="handler">Event handler for connection lifecycle events, or nullptr to use a default handler.</param>
   /// <returns>The constructed telemetry client, owned by this ViewerSketch.</returns>
   ///
   TelemetrySubscriber* beginTelemetry(const char* topic, TelemetryEventHandler* handler = nullptr)
   {
      _client = new TelemetrySubscriber(topic, _status, handler);
      _telemetryFeature.connect(_arduino, _status, _client);
      return _client;
   }

   ///
   /// <summary>
   /// Gets the telemetry client constructed by beginTelemetry(), or nullptr if it hasn't
   /// been called yet.
   /// </summary>
   /// <returns>The telemetry client.</returns>
   ///
   TelemetrySubscriber* getClient() const
   {
      return _client;
   }
};
