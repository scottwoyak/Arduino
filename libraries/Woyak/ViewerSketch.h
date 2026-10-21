#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID and
// WIFI_PASSWORD are defined). This mirrors the include order used by MonitorSketch-/
// PublisherSketch-based sketches (see InfluxSketchBase.h).

#include <functional>
#include <span>
#include <sstream>
#include <string>

#include "ArduinoBase.h"
#include "DeviceServerClient.h"
#include "Logger.h"
#include "OTAUpdater.h"
#include "SiteLocationResolver.h"
#include "SketchBase.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "TelemetryConfig.h"
#include "TopicResolver.h"

///
/// <summary>
/// Owns the boot/init sequence shared by every display-only "Viewer" sketch (e.g.
/// Gate_Viewer, Wind_Viewer): printing the sketch name/version startup info to Serial
/// and the display, connecting to WiFi, and (optionally) enabling OTA firmware updates.
/// Unlike MonitorSketch/PublisherSketch (see InfluxSketchBase), a Viewer doesn't post to
/// InfluxDB; it only renders telemetry it receives. A sketch constructs a ViewerSketch,
/// calls begin() once from setup() (after registering its own telemetry client(s) and
/// before any sketch-specific WiFi-dependent setup), and calls loop() once from loop().
/// </summary>
///
class ViewerSketch : public SketchBase
{
protected:
   /// <summary>Telemetry settings, copied from the constructor argument.</summary>
   TelemetryConfig _telemetryConfig;

   /// <summary>Resolves the telemetry topic (from telemetryConfig) and connects the telemetry client.</summary>
   TopicResolver _topicResolver;

   /// <summary>Resolves a free-text site/location pair, if siteOptions is non-empty; see resolveSiteLocation().</summary>
   SiteLocationResolver _siteLocationResolver;

   /// <summary>Constructed by beginTelemetry(), once the telemetry topic has been resolved.</summary>
   TelemetrySubscriber* _client = nullptr;

   /// <summary>The single instance, used by the static _onViewerCommand() bridge function (DeviceServerClient::onConfigCommand() only accepts a captureless function pointer).</summary>
   inline static ViewerSketch* _viewerInstance = nullptr;

   /// <summary>Commands handled directly by ViewerSketch, listed by "Help" alongside the built-ins.</summary>
   static constexpr DeviceServerClient::CommandInfo VIEWER_COMMANDS[] = {
      { "GetTopic", "Reports the current telemetry topic", { "gt" }, {}, false, { "Telemetry" } },
      {
         "SetTopic",
         "Persists a new telemetry topic (only if the sketch offers a topic table); requires Restart to take effect",
         { "st" },
         {
            { "topic", "Telemetry topic name", true },
         },
         false,
         { "Telemetry" },
      },
   };

   ///
   /// <summary>
   /// Handles the "GetTopic"/"SetTopic" commands registered by beginTelemetry().
   /// Registered with DeviceServerClient::onConfigCommand() since it only accepts a
   /// captureless function pointer.
   /// </summary>
   /// <param name="command">Full command text received from the DeviceServer.</param>
   /// <returns>True if the command was recognized and handled; otherwise false.</returns>
   ///
   static bool _onViewerCommand(const char* command)
   {
      std::istringstream stream(command);
      std::string verb;
      stream >> verb;

      if (strcasecmp(verb.c_str(), "GetTopic") == 0)
      {
         const char* resolved = _viewerInstance->_topicResolver.topic();
         std::string topic = resolved != nullptr ? resolved : (_viewerInstance->_client != nullptr ? _viewerInstance->_client->getTopic() : "");
         DeviceServerClient::respond(topic.c_str());
         return true;
      }
      if (strcasecmp(verb.c_str(), "SetTopic") == 0)
      {
         std::string value;
         std::getline(stream >> std::ws, value);
         DeviceServerClient::respond(_viewerInstance->_topicResolver.trySetTopic(value).c_str());
         return true;
      }
      return false;
   }

public:
   ///
   /// <summary>
   /// Creates a ViewerSketch bound to the given board and configuration.
   /// </summary>
   /// <param name="config">Shared configuration; preferencesNamespace is used by resolveTopic()/resolveSiteLocation().</param>
   /// <param name="telemetryConfig">Telemetry settings (topic table or fixed topic) used by resolveTopic(). Defaults to empty for a viewer that passes its topic directly to beginTelemetry(topic, handler).</param>
   /// <param name="siteOptions">Site choices offered by resolveSiteLocation(); location is entered as free text. Leave empty for a viewer that doesn't prompt for a site/location.</param>
   ///
   ViewerSketch(const SketchConfig& config, const TelemetryConfig& telemetryConfig = {}, std::span<const char* const> siteOptions = {})
      : SketchBase(config),
        _telemetryConfig(telemetryConfig),
        _topicResolver(config.preferencesNamespace, &_telemetryConfig),
        _siteLocationResolver("Device", siteOptions, config.preferencesNamespace)
   {
   }

   ///
   /// <summary>
   /// Prints the sketch name/version startup info to Serial and the display.
   /// from setup(), before any sketch-specific prompting (e.g. resolveTopic()) that must
   /// happen before WiFi connects, followed by beginConnect().
   /// </summary>
   ///
   void beginBanner()
   {
      _checkSafeMode();
      Util::setSetupStep("Board begin");
      arduino.begin();
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
      if (!forcePrompt)
      {
         Serial.println("Press buttonA or touch the screen now to reconfigure the topic...");
         forcePrompt = PreferencesResolver::waitForForcePrompt(_arduino, 2000);
      }

      Util::setSetupStep("Topic configuration");
      const char* topic = _topicResolver.resolve(_arduino, _status, forcePrompt);
      _printAndLogStatus("Topic... ", ("\"" + std::string(topic) + "\"").c_str());
      return topic;
   }

   ///
   /// <summary>
   /// Resolves this viewer's site/location (see SiteLocationResolver::resolve()), prompting
   /// (or loading the saved values from Preferences) if needed, and registers
   /// the "GetConfig"/"SetConfig" commands with DeviceServerClient so the resolved values
   /// can be reported/changed remotely. Also reports the resolved site/location as an init
   /// status line. Call once from setup(), after beginBanner() and before beginConnect().
   /// Only applies if siteOptions was non-empty in the constructor. Only one
   /// DeviceServerClient::onCommand() handler is supported, so don't call this if the
   /// sketch registers its own handler.
   /// </summary>
   /// <param name="forcePrompt">If true, always offers to reconfigure even if saved values exist.</param>
   ///
   void resolveSiteLocation(bool forcePrompt = false)
   {
      if (!forcePrompt)
      {
         Serial.println("Press buttonA or touch the screen now to reconfigure the location...");
         forcePrompt = PreferencesResolver::waitForForcePrompt(_arduino, 2000);
      }

      Util::setSetupStep("Location configuration");
      _siteLocationResolver.resolve(_arduino, forcePrompt);
      _printAndLogStatus("Location... ", siteLocation().c_str());
   }

   ///
   /// <summary>Returns the resolved site name (empty until resolveSiteLocation() is called).</summary>
   ///
   const String& site() const
   {
      return _siteLocationResolver.site();
   }

   ///
   /// <summary>Returns the resolved location name (empty until resolveSiteLocation() is called).</summary>
   ///
   const String& location() const
   {
      return _siteLocationResolver.location();
   }

   ///
   /// <summary>Returns the resolved "site/location" string (empty until resolveSiteLocation() is called).</summary>
   ///
   std::string siteLocation() const
   {
      return std::string(site().c_str()) + "/" + location().c_str();
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
      ASSERT(_topicResolver.topic() != nullptr);

      return beginTelemetry(_topicResolver.topic(), handler);
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
      _client = new TelemetrySubscriber(topic, _telemetryConfig, _status, handler);
      _client->connect(_arduino, _status);

      _viewerInstance = this;
      DeviceServerClient::onConfigCommand(_onViewerCommand, VIEWER_COMMANDS, std::size(VIEWER_COMMANDS));
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
