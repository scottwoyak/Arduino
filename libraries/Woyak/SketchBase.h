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
#include "DeviceHealth.h"
#include "Logger.h"
#include "OTAUpdater.h"
#include "Status.h"
#include "Util.h"

#ifdef ARDUINO_DISPLAY_SUPPORTED
#include "Table.h"
#endif

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

   /// <summary>CPU clock speed (in MHz) set once begin() completes. Field-deployed sketches should set a lower value (e.g. 80) to reduce power draw/heat.</summary>
   uint8_t cpuFrequencyMhz = 240;

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
   /// <summary>Board wrapper, owned by the sketch. Declared first so it's constructed before the members below that use it.</summary>
   Arduino arduino;

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

   /// <summary>Seconds the initialization info remains on the display before completeInitialization() clears it. See completeInitialization().</summary>
   static constexpr uint8_t STARTUP_DELAY_S = 5;

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

   /// <summary>The single SketchBase instance.</summary>
   inline static SketchBase* _instance = nullptr;

   /// <summary>Callback registered via setOnWiFiLostCallback(), used by _onWiFiLost().</summary>
   std::function<bool()> _onWiFiLostCallback = nullptr;

   /// <summary>Seconds a Locate request lasts.</summary>
   static constexpr uint8_t LOCATE_DURATION_S = 30;

   /// <summary>Milliseconds between LED toggles while locating.</summary>
   static constexpr uint16_t LOCATE_FLASH_INTERVAL_MS = 100;

   /// <summary>True while a Locate request is active.</summary>
   bool _locating = false;

   /// <summary>Callback registered via setOnLocateEndCallback().</summary>
   std::function<void()> _onLocateEndCallback = nullptr;

   /// <summary>Ends the Locate request.</summary>
   TimerSecs _locateTimer = TimerSecs(LOCATE_DURATION_S);

   /// <summary>Paces the LED flashing while locating.</summary>
   Timer _locateFlashTimer = Timer(LOCATE_FLASH_INTERVAL_MS);

   /// <summary>Current LED state while flashing.</summary>
   bool _locateLedOn = false;

   /// <summary>Number of WiFi disconnect events not yet logged (set from the WiFi event task).</summary>
   static inline volatile uint32_t _wifiDisconnects = 0;

   /// <summary>Reason code of the most recent WiFi disconnect event.</summary>
   static inline volatile uint8_t _wifiDisconnectReason = 0;

   /// <summary>Set when WiFi obtains an IP address; cleared once logged.</summary>
   static inline volatile bool _wifiGotIp = false;

   ///
   /// <summary>
   /// WiFi event callback. Runs on the WiFi event task, so it only records state; the
   /// loop task logs it (see _logWiFiEvents()) since Logger isn't thread safe.
   /// </summary>
   /// <param name="event">The WiFi event</param>
   /// <param name="info">Event details</param>
   ///
   static void _onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info)
   {
      if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
      {
         _wifiDisconnectReason = info.wifi_sta_disconnected.reason;
         _wifiDisconnects = _wifiDisconnects + 1;
      }
      else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)
      {
         _wifiGotIp = true;
      }
   }

   ///
   /// <summary>
   /// Logs any WiFi disconnects and reconnects recorded by _onWiFiEvent(). Call from the loop task.
   /// </summary>
   ///
   void _logWiFiEvents()
   {
      const uint32_t numDisconnects = _wifiDisconnects;
      if (numDisconnects > 0)
      {
         _wifiDisconnects = 0;
         Logger.log(
            "WiFi disconnected " + std::to_string(numDisconnects) + " time(s), last reason code " + std::to_string(_wifiDisconnectReason),
            LogSeverity::WARN,
            "WiFi");
      }

      if (_wifiGotIp)
      {
         _wifiGotIp = false;
         Logger.log("WiFi connected, RSSI " + std::to_string(WiFi.RSSI()) + " dBm", LogSeverity::INFO, "WiFi");
      }
   }

   /// <summary>Seconds last shown in the Locate countdown footer.</summary>
   int16_t _locateShownSecs = -1;

   ///
   /// <summary>
   /// Draws the gray countdown footer (seconds until the normal sketch returns), but only
   /// when the displayed number of seconds changes.
   /// </summary>
   ///
   void _drawLocateFooter()
   {
#ifdef ARDUINO_DISPLAY_SUPPORTED
      int16_t secs = (int16_t)ceilf(_locateTimer.remaining());
      if (secs == _locateShownSecs)
      {
         return;
      }
      _locateShownSecs = secs;

      std::string text = "Returning in " + std::to_string(secs) + "s ";
      _arduino->setTextSize(2);
      _arduino->setCursor(_arduino->width(), _arduino->height() - _arduino->charH());
      _arduino->printR(text, Color::DARKGRAY, Color::BLACK);
#endif
   }

   ///
   /// <summary>
   /// Starts a Locate request. Boards with a display show the sketch name, version, IP and
   /// MAC address; other boards rapidly flash their status LED. Lasts LOCATE_DURATION_S.
   /// </summary>
   ///
   void _startLocate()
   {
      _locating = true;
      _locateTimer.reset();
      _locateFlashTimer.reset();
      _locateLedOn = false;

#ifdef ARDUINO_DISPLAY_SUPPORTED
      _arduino->clearDisplay();

      std::string name = _config.sketchName;
      std::string version = _config.version != nullptr ? _config.version : "";
      std::string ip = WiFi.localIP().toString().c_str();
      std::string mac = WiFi.macAddress().c_str();

      Table table(_arduino, 0, 0);
      table.addRow("Sketch", std::string(name.length(), ' '));
      table.addRow("Version", std::string(version.length(), ' '));
      table.addRow("IP", std::string(ip.length(), ' '));
      table.addRow("MAC", std::string(mac.length(), ' '));
      table.setValue(0, name);
      table.setValue(1, version);
      table.setValue(2, ip);
      table.setValue(3, mac);
      table.setPosition(_arduino->width() / 2, _arduino->height() / 2, Anchor::CENTER);
      table.draw();

      _locateShownSecs = -1;
      _drawLocateFooter();
#endif
   }

   ///
   /// <summary>
   /// Services an active Locate request: flashes the status LED on boards without a
   /// display, and ends the request after LOCATE_DURATION_S. On display boards the display
   /// is cleared at the end, then _onLocateEndCallback is invoked so the sketch can redraw.
   /// </summary>
   ///
   void _updateLocate()
   {
      if (!_locating)
      {
         return;
      }

      if (_locateTimer.expired())
      {
         _locating = false;
#ifdef ARDUINO_DISPLAY_SUPPORTED
         _arduino->clearDisplay();
#else
         _status->setStatus(Status::RUNNING);
         _status->off();
#endif
         if (_onLocateEndCallback != nullptr)
         {
            _onLocateEndCallback();
         }
         return;
      }

#ifdef ARDUINO_DISPLAY_SUPPORTED
      _drawLocateFooter();
#endif

#ifndef ARDUINO_DISPLAY_SUPPORTED
      if (_locateFlashTimer.ready())
      {
         _locateLedOn = !_locateLedOn;
         if (_locateLedOn)
         {
            _status->setStatus(Status::RUNNING);
         }
         else
         {
            _status->off();
         }
      }
#endif
   }

   ///
   /// <summary>
   /// Sends a text log message to the LogServer, tagged implicitly by the handshake
   /// (deviceId/sketch/version) sent when the connection was established. Does nothing
   /// (other than the Serial echo performed by Logger.log()) if the LogServer
   /// connection isn't up yet.
   /// </summary>
   /// <param name="message">Message to log, both to the LogServer and Serial.</param>
   /// <param name="severity">Severity of the message, sent to the server as its level.</param>
   ///
   void _logMessage(const char* message, LogSeverity severity = LogSeverity::INFO)
   {
      Logger.log(message, severity);
   }

   ///
   /// <summary>
   /// Sends a text log message tagged with "OTA".
   /// </summary>
   /// <param name="message">Message to log.</param>
   /// <param name="severity">Severity of the message.</param>
   ///
   void _logOTA(const std::string& message, LogSeverity severity = LogSeverity::INFO)
   {
      Logger.log(message, severity, "OTA");
   }

   ///
   /// <summary>
   /// Overload of _logMessage(const char*, LogSeverity) accepting a std::string so
   /// callers don't need to call .c_str() themselves.
   /// </summary>
   /// <param name="message">Message to log, both to the LogServer and Serial.</param>
   /// <param name="severity">Severity of the message, sent to the server as its level.</param>
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
      _arduino->printInitLabel(label);
      Logger.logPartial(label);
   }

   ///
   /// <summary>Completes a status line started via _logStatusStart(), printing (on display-capable boards) and logging the result fragment.</summary>
   /// <param name="result">The result fragment to print/log, e.g. "OK".</param>
   ///
   void _logStatusEnd(const char* result)
   {
      _arduino->printInitValue(result);
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

      const char* sketchName = _config.sketchName;
      if (_arduino->fitsOnDisplay("Sketch... ", sketchName))
      {
         _arduino->printlnInitStatus("Sketch... ", sketchName);
      }
      else
      {
         std::string abbreviation;
         bool startOfWord = true;
         for (const char* c = sketchName; *c != '\0'; c++)
         {
            if (*c == '_')
            {
               startOfWord = true;
            }
            else if (startOfWord)
            {
               abbreviation += *c;
               startOfWord = false;
            }
         }
         _arduino->printlnInitStatus("Sketch... ", abbreviation.c_str(), sketchName);
      }

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
   /// connection. If the initial WiFi connection fails, there's no point attempting
   /// any of that WiFi-dependent setup, so the device resets immediately instead.
   /// </summary>
   ///
   void _beginConnect()
   {
      if (!_arduino->initWifi(WIFI_SSID, WIFI_PASSWORD, _status))
      {
         Util::reset(WIFI_LOST_RESET_DELAY_S, "WiFi connect failed");
      }

      WiFi.onEvent(_onWiFiEvent);

      if (_config.enableRebooter)
      {
         _arduino->enableRebooter();
      }

      if (_config.enableOTA)
      {
         _arduino->enableOTA(_config.version, _config.sketchName, _status, this);
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
      _beginLoggerConnection(site, location);
      _finishLoggerSetup();
   }

   ///
   /// <summary>
   /// Starts the Logger connection and blocks (via waitForClient()) until the "Hub... "
   /// label printed by begin() is completed by Logger::_onEvent(), so nothing else may log a
   /// line until then. Logger itself remains async. Call right after _beginConnect() so
   /// failures during the rest of initialization (e.g. sensors) are captured by the
   /// LogServer, then call _finishLoggerSetup() as the last setup step.
   /// </summary>
   /// <param name="site">Optional site tag to associate with the LogServer connection.</param>
   /// <param name="location">Optional location tag to associate with the LogServer connection.</param>
   ///
   void _beginLoggerConnection(const char* site = nullptr, const char* location = nullptr)
   {
      // DeviceServerClient::begin() only echoes its "Hub... " label to Serial/the Device
      // Hub itself (it has no reference to the display, being a static-only API); print the
      // label to the display here too, the same way ArduinoBase::initClient() does for other
      // clients, so the completion text printed below has something to follow.
      _arduino->printInitLabel("Hub... ");

      DeviceServerClient::begin(_config.sketchName, _config.version, site, location);
      _arduino->waitForClient([]() { return DeviceServerClient::isResolved(); }, []() { DeviceServerClient::loop(); });

      // DeviceServerClient::log() only echoes to Serial/the Device Server itself (it has no
      // reference to the display, being a static-only API); print the completion text to
      // the display here too, the same way TelemetryClient::connect() does for Telemetry.
      if (DeviceServerClient::isConnected())
      {
         _arduino->printInitValue(DeviceServerClient::isDirectConnection() ? DeviceServerClient::getHost().c_str() : "OK");
      }
      else
      {
         _arduino->printInitValue("FAILED", Color::RED);
      }
   }

   ///
   /// <summary>
   /// Completes Logger setup once initialization is done: applies the CPU frequency
   /// and enables the watchdog. Deferred until the end of
   /// setup so the watchdog doesn't trip during the blocking init steps.
   /// </summary>
   ///
   void _finishLoggerSetup()
   {
      setCpuFrequencyMhz(_config.cpuFrequencyMhz);

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
   /// Runs the standard per-loop step
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
      DeviceHealth::recordLoop();

      _logWiFiEvents();

      checkForOTA();

      DeviceServerClient::loop();

      _arduino->updateStatusIndicators();
      _updateLocate();
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

            std::string reason = std::string("Sensor '") + sensor.label + "' failed to initialize";
            _printAndLogStatus(("Restarting in " + std::to_string(_config.sensorFailureResetDelayS) + "s").c_str(), reason + ". Restarting in " + std::to_string(_config.sensorFailureResetDelayS) + "s", Color::RED);
            Util::reset(_config.sensorFailureResetDelayS, reason);
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
      // The device restarts right after this returns, so keep servicing the Device Server
      // connection briefly to let any pending messages (e.g. "Updating firmware to ...") actually get sent.
      Util::setHaltReason("Firmware Update");

      constexpr uint16_t RESTART_FLUSH_MS = 500;
      TimerMillis flushTimer(RESTART_FLUSH_MS);
      while (!flushTimer.expired())
      {
         DeviceServerClient::loop();
         delay(10);
      }
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
      bool isError = text.find("failed") != std::string::npos || text.find("No OTA download partition") != std::string::npos;

      _logOTA(text, isError ? LogSeverity::ERROR : LogSeverity::INFO);
   }

public:
   ///
   /// <summary>
   /// Creates a SketchBase with its own board wrapper (the "arduino" member), which is
   /// used as the status indicator directly if it implements IStatus itself; otherwise
   /// its onboard NeoPixel LED is used.
   /// </summary>
   /// <param name="config">Shared configuration.</param>
   ///
   SketchBase(const SketchConfig& config)
      :
#ifndef ARDUINO_STATUS_SUPPORTED
        _ownedNeoPixelStatus(&arduino.neoPixel),
#endif
        _config(config),
        _arduino(&arduino),
#ifdef ARDUINO_STATUS_SUPPORTED
        _status(&arduino)
#else
        _status(&_ownedNeoPixelStatus)
#endif
   {
      ASSERT(_instance == nullptr);

      _instance = this;
      DeviceServerClient::onLocate([]() { _instance->_startLocate(); });
   }

   ///
   /// <summary>
   /// Gets the status indicator (LED) driven by this sketch. Pass it to a telemetry client
   /// created directly by a sketch so the client can set the indicator to RUNNING once connected.
   /// </summary>
   /// <returns>The status indicator.</returns>
   ///
   IStatus* getStatus() const
   {
      return _status;
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

      ///
      /// <summary>
      /// Gets whether a Locate request is active. Sketches that draw to the display should
      /// skip their drawing while this is true so the locate info stays visible.
      /// </summary>
      /// <returns>True while locating.</returns>
      ///
      bool isLocating() const
      {
         return _locating;
      }

      ///
      /// <summary>
      /// Registers a callback invoked when a Locate request ends. On display boards the
      /// display has just been cleared, so use this to redraw the sketch's static content
      /// and force its next update to repaint everything.
      /// </summary>
      /// <param name="callback">Called with no arguments.</param>
      ///
      void setOnLocateEndCallback(std::function<void()> callback)
      {
         _onLocateEndCallback = callback;
      }

      ///
      /// <summary>
      /// Completes the boot/init sequence: on display-capable boards, pauses for
      /// STARTUP_DELAY_S seconds so the initialization info printed to the display remains
      /// readable, then clears the display; serial-only boards have no init screen to
      /// preserve, so the pause is skipped. Always logs the standard "initialization
      /// complete" message. Call once, at the very end of the sketch's setup(), after any
      /// sketch-specific setup (e.g. registering telemetry handlers, building tables) that
      /// should still appear on the init screen.
      /// </summary>
      ///
      void completeInitialization()
      {
#ifdef ARDUINO_DISPLAY_SUPPORTED
         delay(STARTUP_DELAY_S * 1000UL);
         _arduino->clearDisplay();
#endif
         Logger.logInitializationComplete();
      }
   };
