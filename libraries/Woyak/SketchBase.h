#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID and
// WIFI_PASSWORD are defined). This mirrors the include order used by
// InfluxSketchBase-derived (Monitor/Publisher) sketches.

#include <functional>
#include <string>
#include <vector>

#include <esp_task_wdt.h>

#include "ArduinoBase.h"
#include "Logger.h"
#include "OTAUpdater.h"
#include "Status.h"
#include "Util.h"

///
/// <summary>
/// Configuration shared by every sketch built on SketchBase: sketch identity, OTA,
/// rebooter, and CPU settings. Influx and telemetry settings are passed separately
/// (as InfluxConfig/TelemetryConfig) to the derived classes that use them.
/// </summary>
///
/// Fields with a default value below only need to be specified by a sketch if it wants
/// to override that default; use designated initializers and list only the fields that
/// differ, e.g. { .sketchName = "Some_Monitor", .version = VERSION,
/// .preferencesNamespace = "Some_Monitor", .enableOTA = true }.
///
struct SketchConfig
{
   /// <summary>Sketch name, printed at boot and used as the OTA update identifier.</summary>
   const char* sketchName;

   /// <summary>Sketch version string, printed at boot (if set) and used for OTA update checks. Leave null for a testing sketch that doesn't track a version or use OTA.</summary>
   const char* version = nullptr;

   /// <summary>Preferences (NVS) namespace used to persist prompted selections (Influx site, telemetry topic). Only needed if a derived class prompts.</summary>
   const char* preferencesNamespace = nullptr;

   /// <summary>CPU clock speed (in MHz) set once begin() completes, to reduce power draw/heat.</summary>
   uint8_t cpuFrequencyMhz = 80;

   /// <summary>If true, enable OTA firmware updates via arduino.enableOTA(). Sketches that need OTA (e.g. remotely deployed ones) must set this to true.</summary>
   bool enableOTA = false;

       /// <summary>If true, enable the scheduled daily reboot via arduino.enableRebooter(). Sketches that need it (e.g. long-running deployed sketches) must set this to true.</summary>
       bool enableRebooter = false;

       /// <summary>Seconds to wait before resetting after a fatal sensor init failure (see addSensor()) or after reportSensorFailure() (MonitorSketch only) is called.</summary>
       uint8_t sensorFailureResetDelayS = 10;
   };

///
/// <summary>
/// Owns the generic boot/init sequence shared by every sketch built on top of it:
/// sketch name/version startup print, WiFi connect, OTA/rebooter enable, Logger
/// connect, CPU clock speed, and the standard per-loop OTA/Logger/status-indicator/
/// watchdog step. Concrete sketch base classes (InfluxSketchBase, ViewerSketch) derive
/// from this and layer their own site/telemetry/sensor logic on top. A sketch
/// constructs the derived class, calls begin() once from setup() (after registering any
/// sketch-specific hooks), and calls loop() once from loop().
/// </summary>
///
class SketchBase : private OTAUpdateEventHandler
{
public:
   ///
   /// <summary>
   /// One sensor to initialize during begin(), via arduino.initSensor(). If fatal is
   /// true, a failed init resets the device; otherwise the sketch continues.
   /// </summary>
   ///
   struct SensorInit
   {
      const char* label;
      bool (*initFunc)();
      const char* (*successLabelFunc)();
      bool fatal;
   };

protected:
   /// <summary>ESP32 task watchdog timeout (in seconds); loop() resets it automatically.</summary>
   static constexpr uint8_t WATCHDOG_INTERVAL_S = 60;

   /// <summary>Seconds to wait before resetting after WiFi connectivity is lost and cannot be reestablished. See _onWiFiLost().</summary>
   static constexpr uint8_t WIFI_LOST_RESET_DELAY_S = 10;

   /// <summary>Fallback status indicator, used when the board doesn't implement IStatus itself.</summary>
#ifndef ARDUINO_STATUS_SUPPORTED
   NeoPixelStatus _ownedNeoPixelStatus;
#endif

   /// <summary>Copy of the config passed to the constructor.</summary>
   SketchConfig _config;

   /// <summary>Sensors registered via addSensor(), run in order during begin().</summary>
   std::vector<SensorInit> _sensors;

   /// <summary>Board wrapper.</summary>
   Arduino* _arduino;

   /// <summary>Status indicator driven through the WiFi/OTA phases of begin().</summary>
   IStatus* _status;

   /// <summary>Callback registered via onStatus(), invoked after any base fields added by _populateStatus() below.</summary>
   void (*_sketchStatusHandler)(LoggerStatus& status) = nullptr;

   /// <summary>The single SketchBase instance, used by _onGetStatus() to reach the instance whose fields it should add (Logger.onStatus() only accepts a captureless function pointer).</summary>
   inline static SketchBase* _instance = nullptr;

   /// <summary>Callback registered via setOnWiFiLostCallback(), used by _onWiFiLost().</summary>
   std::function<bool()> _onWiFiLostCallback = nullptr;

   ///
   /// <summary>
   /// Sends a text log message to the LogServer, tagged implicitly by the handshake
   /// (deviceId/sketch/version) sent when the connection was established. Does nothing
   /// (other than the Serial echo performed by Logger.log()) if the LogServer
   /// connection isn't up yet.
   /// </summary>
   /// <param name="message">Message to log, both to the LogServer and Serial.</param>
   /// <param name="severity">Severity of the message; ERROR is prefixed with "ERROR: ".</param>
   ///
   void _logMessage(const char* message, LogSeverity severity = LogSeverity::INFO)
   {
      Logger.log(message, severity);
   }

   ///
   /// <summary>
   /// Overload of _logMessage(const char*, LogSeverity) accepting a std::string so
   /// callers don't need to call .c_str() themselves.
   /// </summary>
   /// <param name="message">Message to log, both to the LogServer and Serial.</param>
   /// <param name="severity">Severity of the message; ERROR is prefixed with "ERROR: ".</param>
   ///
   void _logMessage(const std::string& message, LogSeverity severity = LogSeverity::INFO)
   {
      _logMessage(message.c_str(), severity);
   }

   ///
   /// <summary>Prints an init header via Arduino::printInitHeader() and also logs it.</summary>
   /// <param name="str">The header text to print and log.</param>
   ///
   void _printAndLog(const char* str)
   {
      _arduino->printInitHeader(str);
      _logMessage(str);
   }

   ///
   /// <summary>Prints a one-off init status line via Arduino::printlnInitStatus(), which also logs it automatically.</summary>
   /// <param name="str">The status text to print and log.</param>
   ///
   void _printAndLogStatus(const char* str)
   {
      _arduino->printlnInitStatus(str);
   }

   ///
   /// <summary>Prints a one-off "label: value" init status line via Arduino::printlnInitStatus(), which also logs the combined text automatically.</summary>
   /// <param name="label">The label text to print.</param>
   /// <param name="value">The value text to print right after the label.</param>
   ///
   void _printAndLogStatus(const char* label, const char* value)
   {
      _arduino->printlnInitStatus(label, value);
   }

   ///
   /// <summary>Prints a brief status line to the display while logging a separate (typically more detailed) string, via Arduino::printlnInitStatus(), which also logs it automatically.</summary>
   /// <param name="displayStr">The status text to print to the display.</param>
   /// <param name="logStr">The status text to log.</param>
   ///
   void _printAndLogStatus(const char* displayStr, const char* logStr, Color textColor)
   {
      _arduino->printlnInitStatus(displayStr, logStr, textColor);
   }

   ///
   /// <summary>
   /// Overload of _printAndLogStatus(const char*, const char*, Color) accepting a
   /// std::string log message so callers don't need to call .c_str() themselves.
   /// </summary>
   /// <param name="displayStr">The status text to print to the display.</param>
   /// <param name="logStr">The status text to log.</param>
   ///
   void _printAndLogStatus(const char* displayStr, const std::string& logStr, Color textColor)
   {
      _printAndLogStatus(displayStr, logStr.c_str(), textColor);
   }

   ///
   /// <summary>
   /// Prints a "label..." fragment to the display (on display-capable boards) and logs it
   /// (no newline yet), followed once the result is known by the completing "result"
   /// fragment via _logStatusEnd(). Mirrors the old two-step
   /// Serial.print(label)/Serial.println(result) pattern using Logger.logPartial()/log().
   /// </summary>
   /// <param name="label">The label fragment to print/log, e.g. "WiFi... ".</param>
   ///
   void _logStatusStart(const char* label)
   {
      _arduino->print(label, Color::LABEL);
      Logger.logPartial(label);
   }

   ///
   /// <summary>Completes a status line started via _logStatusStart(), printing (on display-capable boards) and logging the result fragment.</summary>
   /// <param name="result">The result fragment to print/log, e.g. "OK".</param>
   ///
   void _logStatusEnd(const char* result)
   {
      _arduino->printlnR(result, Color::VALUE);
      Logger.log(result);
   }

   ///
   /// <summary>
   /// Prints the sketch name/version startup lines to Serial and the display, as two
   /// separate lines ("Sketch... " / "Version... "). Call once from begin(), before
   /// _beginConnect().
   /// </summary>
   ///
   void _printStartupInfo()
   {
      _arduino->beginInit();
      Logger.log("Initializing");

      _arduino->printlnInitStatus("Sketch... ", _config.sketchName);

      const char* version = _config.version;
      if (version != nullptr)
      {
         const char* versionText = (version[0] == 'v' || version[0] == 'V') ? version + 1 : version;
         _arduino->printlnInitStatus("Version... ", versionText);
      }
   }

   ///
   /// <summary>
   /// Connects to WiFi and enables OTA if configured. Call once from begin(), after
   /// _printStartupInfo() (and, if needed, any sketch-specific prompting that must
   /// happen before WiFi connects). Followed by any WiFi-dependent setup (e.g. a
   /// telemetry client) and finally _beginLogger(), which starts the LogServer
   /// connection.
   /// </summary>
   ///
   void _beginConnect()
   {
      _arduino->initWifi(WIFI_SSID, WIFI_PASSWORD, _status);

      if (_config.enableRebooter)
      {
         _arduino->enableRebooter();
      }

      if (_config.enableOTA)
      {
         _arduino->enableOTA(_config.version, _config.sketchName, _status, OTAUpdater::DEFAULT_CHECK_INTERVAL_SECS, this);
      }
   }

   ///
   /// <summary>
   /// Starts the Logger connection. Call once from begin(), as the last setup step,
   /// after _beginConnect() and any other WiFi-dependent setup (e.g. a telemetry
   /// client), so nothing else can log a line while the LogServer connection is
   /// pending.
   /// </summary>
   /// <param name="site">Optional site tag to associate with the LogServer connection (e.g. the resolved InfluxDB site).</param>
   /// <param name="location">Optional location tag to associate with the LogServer connection.</param>
   ///
   void _beginLogger(const char* site = nullptr, const char* location = nullptr)
   {
      // Started last: blocks (via waitForClient()) until the "Logging... " label printed
      // by begin() is completed by Logger::_onEvent(), so nothing else may log a line
      // until then. Logger itself remains async.
      Logger.begin(_config.sketchName, _config.version, site, location);
      _arduino->waitForClient([]() { return Logger.isResolved(); }, []() { Logger.loop(); });

      setCpuFrequencyMhz(_config.cpuFrequencyMhz);

      Logger.onStatus(_onGetStatus);

      esp_task_wdt_config_t twdtConfig = {
         .timeout_ms = WATCHDOG_INTERVAL_S * 1000U,
         .idle_core_mask = 0,
         .trigger_panic = true,
      };
      esp_task_wdt_reconfigure(&twdtConfig);
      esp_task_wdt_add(nullptr);
   }

   ///
   /// <summary>
   /// Adds base status fields to a GetStatus reply, then invokes the sketch's own
   /// handler (registered via onStatus()), if any. Overridden by derived classes (e.g.
   /// InfluxSketchBase) to add their own fields before calling this base version.
   /// </summary>
   /// <param name="status">The in-progress status to add fields to.</param>
   ///
   virtual void _populateStatus(LoggerStatus& status)
   {
      for (const SensorInit& sensor : _sensors)
      {
         const char* label = (sensor.successLabelFunc != nullptr) ? sensor.successLabelFunc() : "OK";
         status.add(sensor.label, label);
      }

      if (_sketchStatusHandler != nullptr)
      {
         _sketchStatusHandler(status);
      }
   }

   ///
   /// <summary>
   /// Static trampoline registered with Logger.onStatus(), forwarding to the single
   /// SketchBase instance's _populateStatus().
   /// </summary>
   /// <param name="status">The in-progress status to add fields to.</param>
   ///
   static void _onGetStatus(LoggerStatus& status)
   {
      if (_instance != nullptr)
      {
         _instance->_populateStatus(status);
      }
   }

   ///
   /// <summary>
   /// Runs the standard per-loop step shared by every sketch: OTA polling, Logger
   /// polling, status indicator updates, and watchdog reset. Call once from loop(),
   /// then layer any sketch-specific per-loop work on top.
   /// </summary>
   ///
   void _loopStep()
   {
      if (!_arduino->ensureWiFiConnected() && !_onWiFiLost())
      {
         Util::reset(WIFI_LOST_RESET_DELAY_S, "WiFi connection lost");
      }

      esp_task_wdt_reset();

      checkForOTA();

      Logger.loop();

      _arduino->updateStatusIndicators();
   }

   ///
   /// <summary>
   /// Extension point run when arduino.ensureWiFiConnected() reports the connection is
   /// lost, before _loopStep() resets the device. Defaults to the callback registered
   /// via setOnWiFiLostCallback() (if any), or returns false if none was registered.
   /// Return true if the loss was fully handled and _loopStep() should skip its own
   /// default reset; return false to let it reset the device after
   /// WIFI_LOST_RESET_DELAY_S seconds.
   /// </summary>
   /// <returns>True if the WiFi loss was fully handled and no reset is needed.</returns>
   ///
   virtual bool _onWiFiLost()
   {
      return _onWiFiLostCallback != nullptr ? _onWiFiLostCallback() : false;
   }

   ///
   /// <summary>
   /// Runs each sensor registered via addSensor(), in order, via arduino.initSensor().
   /// If a fatal sensor fails to initialize, sets the status to FAILED and resets the
   /// device after config.sensorFailureResetDelayS seconds.
   /// registering sensors.
   /// </summary>
   ///
   void _initSensors()
   {
      for (const SensorInit& sensor : _sensors)
      {
         bool success = _arduino->initSensor(sensor.label, sensor.initFunc, sensor.successLabelFunc);

         if (!success && sensor.fatal)
         {
            _status->setStatus(Status::FAILED);
            Util::reset(_config.sensorFailureResetDelayS, std::string("Sensor '") + sensor.label + "' failed to initialize");
         }
      }
   }

   ///
   /// <summary>
   /// OTAUpdateEventHandler implementation, invoked by OTAUpdater just before it downloads
   /// and installs a newly detected firmware version. Logs the update before it starts.
   /// </summary>
   /// <param name="newVersion">The newly detected version string.</param>
   ///
   void onUpdateAvailable(const char* newVersion) override
   {
      std::string otaMessage = std::string("Updating from ") + _config.version + " to " + newVersion;
      _logMessage(otaMessage);
   }

   ///
   /// <summary>
   /// OTAUpdateEventHandler implementation, invoked by OTAUpdater when a detected update
   /// fails to download/install. Logs the failure.
   /// </summary>
   /// <param name="newVersion">The version that failed to install.</param>
   /// <param name="reason">The error reported by the underlying HTTP update client.</param>
   ///
   void onUpdateFailed(const char* newVersion, const char* reason) override
   {
      std::string otaMessage = std::string("Update to ") + newVersion + " failed: " + reason;
      _logMessage(otaMessage, LogSeverity::ERROR);
   }

   ///
   /// <summary>
   /// OTAUpdateEventHandler implementation, invoked by OTAUpdater when a detected update
   /// downloads and installs successfully, just before the device restarts. Logs the
   /// success.
   /// </summary>
   /// <param name="newVersion">The version that was successfully installed.</param>
   ///
   void onUpdateSucceeded(const char* newVersion) override
   {
      std::string otaMessage = std::string("Updated to ") + newVersion;
      _logMessage(otaMessage);
   }

   ///
   /// <summary>
   /// OTAUpdateEventHandler implementation, invoked for OTA diagnostic messages that are
   /// otherwise only printed to Serial (e.g. version check failures, missing OTA
   /// partition). Mirrors them to the LogServer, tagging error-type messages as ERROR.
   /// </summary>
   /// <param name="message">The diagnostic message.</param>
   ///
   void onLogMessage(const char* message) override
   {
      std::string text(message);
      bool isError = text.find("failed") != std::string::npos || text.find("no OTA download partition") != std::string::npos;

      _logMessage(message, isError ? LogSeverity::ERROR : LogSeverity::INFO);
   }

public:
   ///
   /// <summary>
   /// Creates a SketchBase bound to the given board and configuration.
   /// </summary>
   /// <param name="arduino">The board wrapper (used as the status indicator directly if it implements IStatus itself; otherwise its onboard NeoPixel LED is used).</param>
   /// <param name="config">Shared configuration.</param>
   ///
   SketchBase(Arduino* arduino, const SketchConfig& config)
      : _config(config),
#ifndef ARDUINO_STATUS_SUPPORTED
        _ownedNeoPixelStatus(&arduino->neoPixel),
#endif
        _arduino(arduino),
#ifdef ARDUINO_STATUS_SUPPORTED
        _status(arduino)
#else
        _status(&_ownedNeoPixelStatus)
#endif
   {
      ASSERT(arduino != nullptr);
      ASSERT(_instance == nullptr);

      _instance = this;
   }

   ///
   /// <summary>
   /// Checks for a pending OTA firmware update. Call once from loop(), before any other
   /// per-loop work. Does nothing if OTA wasn't enabled.
   /// </summary>
   ///
   void checkForOTA()
   {
      if (_config.enableOTA)
      {
         _arduino->checkForOTA();
      }
   }

   ///
   /// <summary>
   /// Registers a sensor to be initialized (via arduino.initSensor()) during begin().
   /// </summary>
   /// <param name="label">Sensor label printed during initialization.</param>
   /// <param name="initFunc">Captureless function that initializes the sensor.</param>
   /// <param name="successLabelFunc">Optional captureless function returning a label to print on success instead of "OK".</param>
   /// <param name="fatal">If true, a failed init resets the device.</param>
   ///
   void addSensor(const char* label, bool (*initFunc)(), const char* (*successLabelFunc)() = nullptr, bool fatal = true)
   {
      _sensors.push_back({ label, initFunc, successLabelFunc, fatal });
   }

   ///
   /// <summary>
   /// Registers a handler invoked when a "GetStatus" command is received from the
   /// LogServer, after any base fields added by _populateStatus() (e.g. InfluxSketchBase's
   /// Influx bucket/sensor/telemetry fields). Only one handler is supported; call once
   /// from setup(), after begin().
   /// </summary>
   /// <param name="handler">Function invoked with the in-progress status to add fields to.</param>
   ///
      void onStatus(void (*handler)(LoggerStatus& status))
      {
         _sketchStatusHandler = handler;
      }

      ///
      /// <summary>
      /// Registers a callback invoked when WiFi connectivity is lost and cannot be
      /// reestablished (see _onWiFiLost()). Use this to show something on the display,
      /// log a message, etc. before the device resets. Return true from the callback if
      /// the loss was fully handled and loop() should skip its own default reset; return
      /// false to let loop() reset the device after WIFI_LOST_RESET_DELAY_S seconds.
      /// </summary>
      /// <param name="callback">Called with no arguments; returns true if fully handled.</param>
      ///
      void setOnWiFiLostCallback(std::function<bool()> callback)
      {
         _onWiFiLostCallback = callback;
      }
   };
