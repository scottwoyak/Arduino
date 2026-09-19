#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID and
// WIFI_PASSWORD are defined). This mirrors the include order used by Monitor-/
// Publisher-based sketches (see SketchBase.h).

#include <functional>
#include <string>

#include "ArduinoBase.h"
#include "Logger.h"
#include "SiteConfig.h"
#include "Status.h"
#include "TelemetryClient.h"

///
/// <summary>
/// Owns the boot/init sequence shared by every display-only "Viewer" sketch (e.g.
/// Gate_Viewer, Wind_Viewer): printing the sketch name/version banner to Serial and the
/// display, connecting to WiFi, and (optionally) enabling OTA firmware updates. Unlike
/// Monitor/Publisher (see SketchBase), a Viewer doesn't post to InfluxDB or track a
/// site/location; it only renders telemetry it receives. A sketch constructs a
/// ViewerSketch, calls begin() once from setup() (after registering its own telemetry
/// client(s) and before any sketch-specific WiFi-dependent setup), and calls
/// checkForOTA() once from loop().
/// </summary>
///
class ViewerSketch
{
protected:
   /// <summary>Board wrapper.</summary>
   Arduino* _arduino;

   /// <summary>Sketch name, printed at boot and used as the OTA update identifier.</summary>
   const char* _sketchName;

   /// <summary>Sketch version string, printed at boot and used for OTA update checks. Leave null to skip OTA entirely.</summary>
   const char* _version;

   /// <summary>Status indicator driven through the WiFi/OTA phases of begin().</summary>
   IStatus* _status;

   /// <summary>If true, enables OTA firmware updates via arduino.enableOTA() at the end of begin().</summary>
   bool _enableOTA;

   /// <summary>Resolves/persists the selected telemetry topic; constructed on first use by resolveTopic().</summary>
   TelemetryTopicResolver* _topicResolver = nullptr;

   /// <summary>Constructed by beginTelemetry(topic, handler), once the telemetry topic has been resolved.</summary>
   TelemetrySubscriber* _client = nullptr;

   public:
   ///
   /// <summary>
   /// Creates a ViewerSketch bound to the given board, sketch identity, and status
   /// indicator.
   /// </summary>
   /// <param name="arduino">The board wrapper.</param>
   /// <param name="sketchName">Sketch name, printed at boot and used as the OTA update identifier.</param>
   /// <param name="version">Sketch version string, printed at boot and used for OTA update checks.</param>
   /// <param name="status">Status indicator driven through the WiFi/OTA phases of begin().</param>
   /// <param name="enableOTA">If true, enables OTA firmware updates at the end of begin().</param>
   ///
   ViewerSketch(Arduino* arduino, const char* sketchName, const char* version, IStatus* status, bool enableOTA = false)
      : _arduino(arduino), _sketchName(sketchName), _version(version), _status(status), _enableOTA(enableOTA)
   {
      ASSERT(arduino != nullptr);
   }

   ///
   /// <summary>
   /// Prints the sketch name/version banner to Serial and the display, connects to
   /// WiFi, and enables OTA if configured. Call once from setup(), after registering any
   /// telemetry client handlers so they're ready to start connecting once WiFi is up.
   /// </summary>
   ///
   void begin()
   {
      _arduino->beginInit();
      logger().log("Initializing");

      if (_version != nullptr)
      {
         std::string sketchAndVersion = std::string(_sketchName) + ", " + _version;
         _arduino->printlnInitStatus("Sketch... ", sketchAndVersion.c_str());
      }
      else
      {
         _arduino->printlnInitStatus("Sketch... ", _sketchName);
      }

      _arduino->initWifi(WIFI_SSID, WIFI_PASSWORD, _status);

      logger().begin(_sketchName, _version);

      if (_enableOTA)
      {
         _arduino->enableOTA(_version, _sketchName);
      }
   }

   ///
   /// <summary>
   /// Checks for a pending OTA firmware update. Call once from loop(), before any other
   /// per-loop work. Does nothing if OTA wasn't enabled.
   /// </summary>
   ///
   void checkForOTA()
   {
      if (_enableOTA)
      {
         _arduino->checkForOTA();
      }
   }

   void loop()
   {
      checkForOTA();

      logger().loop();

      if (_client != nullptr)
      {
         _client->loop();
      }

      _arduino->updateStatusIndicators();
   }

   ///
   /// <summary>
   /// Resolves which telemetry topic this viewer should subscribe to: the value saved
   /// in Preferences (NVS) under preferencesNamespace, unless it hasn't been saved yet
   /// or forcePrompt is true, in which case the user is prompted over Serial (from
   /// topics) and the choice is saved for next time. Mirrors the topic resolution
   /// SketchBase/TelemetryTopicResolver performs for Publisher sketches. Also reports
   /// the resolved topic via printlnInitStatus(), so it shows up on the display like
   /// other init status lines. Call once from setup(), after begin(), and before
   /// constructing the telemetry client(s) and calling arduino.initClient().
   /// </summary>
   /// <param name="preferencesNamespace">Preferences (NVS) namespace to read/write.</param>
   /// <param name="promptHeader">Prompt header text, e.g. "Select telemetry topic:".</param>
   /// <param name="topics">Telemetry topic table to choose from.</param>
   /// <param name="count">Number of entries in topics.</param>
   /// <param name="forcePrompt">If true, always prompts even if a saved topic exists.</param>
   /// <returns>The resolved topic, backed by this ViewerSketch's storage.</returns>
   ///
   const char* resolveTopic(const char* preferencesNamespace, const char* promptHeader, const char* const topics[], size_t count, bool forcePrompt = false)
   {
      if (_topicResolver == nullptr)
      {
         _topicResolver = new TelemetryTopicResolver(preferencesNamespace);
      }

      const char* topic = _topicResolver->resolve(_arduino->preferences, _status, promptHeader, topics, count, forcePrompt);
      _arduino->printlnInitStatus("Topic... ", topic);
      return topic;
   }

   ///
   /// <summary>
   /// Constructs the TelemetrySubscriber for the given topic and begins connecting it,
   /// printing the standard "Telemetry..." init status line (completed once the client
   /// connects or fails) using this ViewerSketch's status indicator. Mirrors how
   /// Publisher/SketchBase owns its telemetry client. Call once from setup(), after
   /// resolveTopic() and any layout setup that depends on the topic.
   /// </summary>
   /// <param name="topic">The telemetry topic to subscribe to.</param>
   /// <param name="handler">Event handler for connection lifecycle events, or nullptr to use a default handler.</param>
   /// <returns>The constructed telemetry client, owned by this ViewerSketch.</returns>
   ///
   TelemetrySubscriber* beginTelemetry(const char* topic, TelemetryEventHandler* handler = nullptr)
   {
      _client = new TelemetrySubscriber(topic, _status, handler);
      _arduino->initClient("Telemetry", [this]() { _client->beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, _status);
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
