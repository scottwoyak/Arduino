#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID,
// WIFI_PASSWORD, INFLUXDB_URL, and INFLUXDB_ORG are defined). This mirrors the include
// order already used by Monitor-/Publisher-based sketches.

#include <functional>
#include <vector>

#include <esp_task_wdt.h>

#include "Influx.h"
#include "ESP32TempSensor.h"
#include "SerialX.h"
#include "SiteConfig.h"
#include "Status.h"
#include "TempSensor.h"
#include "Timer.h"
#include "Util.h"

///
/// <summary>
/// Configuration shared by Monitor and Publisher sketches: sketch identity, InfluxDB
/// settings (see InfluxConfig), and Publisher's telemetry settings. Fields that only
/// apply to one sketch type (e.g. telemetry for Publisher) are simply left at their
/// default when not used.
/// </summary>
///
/// Fields with a default value below only need to be specified by a sketch if it wants
/// to override that default; use designated initializers and list only the fields that
/// differ, e.g. { .sketchName = "Some_Monitor", .version = VERSION,
/// .preferencesNamespace = "Some_Monitor", .influx = { .prompts = SOME_INFLUX_SITES },
/// .includeCpuTemp = true }.
///
struct SketchConfig
{
   /// <summary>Sketch name, printed at boot and used as the OTA update identifier.</summary>
   const char* sketchName;

   /// <summary>Sketch version string, printed at boot (if set) and used for OTA update checks. Leave null for a testing sketch that doesn't track a version or use OTA.</summary>
   const char* version = nullptr;

   /// <summary>Preferences (NVS) namespace used to persist the selected site/location. Only needed if sites is non-empty.</summary>
   const char* preferencesNamespace = nullptr;

   /// <summary>InfluxDB settings: measurement names, post cadence, rolling-average sample count, and site selection.</summary>
   InfluxConfig influx;

   /// <summary>Publisher only: telemetry settings (selectable topic table or fixed topic, decimals, publish cadence).</summary>
   TelemetryConfig telemetry;

   /// <summary>CPU clock speed (in MHz) set once begin() completes, to reduce power draw/heat.</summary>
   uint8_t cpuFrequencyMhz = 80;

   /// <summary>If true, capture and upload rolling-averaged enclosure temperature/humidity to InfluxDB.</summary>
   bool includeEnclosureTemp = false;

   /// <summary>If true, capture and upload the ESP32 CPU temperature to InfluxDB.</summary>
   bool includeCpuTemp = false;

   /// <summary>If true, enable OTA firmware updates via arduino.enableOTA(). Sketches that need OTA (e.g. remotely deployed ones) must set this to true.</summary>
   bool enableOTA = false;

   /// <summary>If true, enable the scheduled daily reboot via arduino.enableRebooter(). Sketches that need it (e.g. long-running deployed sketches) must set this to true.</summary>
   bool enableRebooter = false;
};

///
/// <summary>
/// Owns the initialization/loop logic shared by Monitor and Publisher: banner,
/// force-prompt window, sensor init, site resolution, WiFi, rebooter, OTA, InfluxDB
/// setup (including the single startup/OTA log point and the standard enclosure/CPU
/// points), and the standard per-loop sensor sampling/Influx post cycle. Behavior that
/// differs between Monitor (no telemetry) and Publisher (telemetry WebSocket client) is
/// factored out into the protected virtual hooks below, overridden by each subclass.
/// </summary>
///
class SketchBase : private OTAUpdateEventHandler
{
public:
   /// <summary>How long to wait after boot for a buttonA press before proceeding.</summary>
   static constexpr uint16_t FORCE_PROMPT_WINDOW_MS = 2000;

   /// <summary>Decimal places used when posting the standard enclosure/CPU fields to InfluxDB.</summary>
   static constexpr uint8_t INFLUX_DECIMALS = 2;

   /// <summary>How often (in milliseconds) the standard enclosure temperature/humidity sensor is sampled.</summary>
   static constexpr uint16_t SENSOR_INTERVAL_MS = 100;

   /// <summary>ESP32 task watchdog timeout (in seconds); loop() resets it automatically.</summary>
   static constexpr uint8_t WATCHDOG_INTERVAL_S = 60;

   /// <summary>Seconds to wait before resetting after WiFi connectivity is lost and cannot be reestablished. See _onWiFiLost().</summary>
   static constexpr uint8_t WIFI_LOST_RESET_DELAY_S = 10;

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
      bool fatal;
   };

protected:
   ///
   /// <summary>
   /// Determines whether the user should be offered a chance to re-prompt for a
   /// site/bucket/location selection right now: automatically true if a Serial monitor
   /// is attached at boot (e.g. the board is inside an enclosure and buttonA can't be
   /// reached), since that's still bounded by a prompt timeout; otherwise gives the user
   /// a short buttonA-held window to force it. buttonA is on GPIO0, a strapping pin:
   /// holding it low during power-on/reset puts the chip into UART download mode instead
   /// of running the sketch, so it can't be checked during boot - hence the window
   /// approach instead of a simple boot-time check.
   /// </summary>
   /// <returns>True if the user should be prompted to reconfigure.</returns>
   ///
   bool _shouldForcePrompt()
   {
      if (Serial)
      {
         return true;
      }

      Serial.println("Press buttonA now to reconfigure the site...");
      return InfluxContextResolver::waitForForcePrompt(_arduino->buttonA, FORCE_PROMPT_WINDOW_MS);
   }

   /// <summary>Board wrapper.</summary>
   Arduino* _arduino;

   /// <summary>Status indicator driving visual feedback during begin()/loop(). Points at the board itself when it implements IStatus (e.g. WaveShare_ESP32_S3_Zero_Sensors's combined external RGB LED/onboard NeoPixel indicator); otherwise falls back to _ownedNeoPixelStatus, driven by the board's onboard NeoPixel LED.</summary>
   IStatus* _status;

#ifndef ARDUINO_STATUS_SUPPORTED
   /// <summary>Fallback status indicator, used when the board doesn't implement IStatus itself.</summary>
   NeoPixelStatus _ownedNeoPixelStatus;
#endif

   /// <summary>Copy of the config passed to the constructor.</summary>
   SketchConfig _config;

   /// <summary>Resolves/persists the chosen InfluxDB site entry.</summary>
   InfluxContextResolver _siteResolver;

   /// <summary>Resolves/persists the chosen telemetry topic.</summary>
   TelemetryTopicResolver _topicResolver;

   /// <summary>The resolved InfluxDB site, populated by begin().</summary>
   InfluxContext _site{};

   /// <summary>The resolved "sensor" tag value, populated by begin() from the resolved site's sensor field.</summary>
   const char* _influxSensor = nullptr;

   /// <summary>The resolved telemetry topic, populated by begin() (nullptr if not used).</summary>
   const char* _telemetryTopic = nullptr;

   /// <summary>Sensors registered via addSensor(), run in order during begin().</summary>
   std::vector<SensorInit> _sensors;

   /// <summary>Owned enclosure temperature/humidity sensor, used if config.includeEnclosureTemp is true.</summary>
   TempSensor _enclosureTempSensor;

   /// <summary>Constructed by begin(), once the InfluxDB bucket has been resolved. Left nullptr if Influx isn't in use (see _shouldUseInflux()).</summary>
   Influx* _influx = nullptr;

   /// <summary>True if Influx is in use; set by begin() from _shouldUseInflux().</summary>
   bool _usesInflux = false;

   /// <summary>Points registered via addPoint() (including the standard enclosure/CPU
   /// points below); posted and flushed together each Influx upload cycle.</summary>
   std::vector<InfluxPoint*> _points;

   InfluxField* _enclosureTempField = nullptr;
   InfluxField* _enclosureHumidityField = nullptr;
   InfluxField* _cpuTempField = nullptr;

   /// <summary>Owned CPU temperature sensor, used if config.includeCpuTemp is true.</summary>
   ESP32TempSensor _cpuTempSensor;

   Timer _sensorTimer;

   /// <summary>Callback registered via setOnWiFiLostCallback(), used by _onWiFiLost().</summary>
   std::function<bool()> _onWiFiLostCallback = nullptr;

   ///
   /// <summary>
   /// Returns the InfluxDB site to use when the config has no selectable influx.prompts
   /// table (i.e. config.influx.prompts is empty). Monitor returns its influx.context;
   /// Publisher returns an empty site (no Influx bucket) since it has no fixed-site
   /// concept of its own.
   /// </summary>
   ///
   virtual InfluxContext _resolveFixedSite() = 0;

   ///
   /// <summary>
   /// Returns the telemetry topic to use when the config has no selectable
   /// telemetry.prompts table (i.e. config.telemetry.prompts is empty). Publisher returns its
   /// fixed config.telemetry.topic (and prints it to Serial); Monitor doesn't use
   /// telemetry, so the default returns nullptr.
   /// </summary>
   ///
   virtual const char* _resolveFixedTelemetryTopic()
   {
      return nullptr;
   }

   ///
   /// <summary>
   /// Returns whether Influx should be initialized this run. Monitor always uses Influx;
   /// Publisher only does so when a selectable Influx site table resolved a bucket.
   /// </summary>
   /// <param name="hasSiteTable">True if config.influx.prompts is non-empty.</param>
   ///
   virtual bool _shouldUseInflux(bool hasSiteTable) = 0;

   ///
   /// <summary>Builds the single startup log message posted once Influx is ready.</summary>
   /// <param name="influxPath">Resolved bucket/measurement/sensor/site/location path.</param>
   ///
   virtual std::string _buildStartupMessage(const std::string& influxPath) = 0;

   ///
   /// <summary>
   /// Extension point run after OTA is enabled, at the end of begin(). Publisher uses
   /// this to construct and start its telemetry WebSocket client; Monitor doesn't need
   /// it.
   /// </summary>
   ///
   virtual void _afterOTASetup()
   {
   }

   ///
   /// <summary>
   /// Extension point run at the very start of loop(), before OTA polling. Publisher
   /// uses this to publish the current value and poll its telemetry client; Monitor
   /// doesn't need it.
   /// </summary>
   ///
   virtual void _beforeOTACheck()
   {
   }

   ///
   /// <summary>
   /// Extension point run when arduino.ensureWiFiConnected() reports the connection is
   /// lost, before loop() resets the device. Defaults to the callback registered via
   /// setOnWiFiLostCallback() (if any), or returns false if none was registered. Return
   /// true if the loss was fully handled and loop() should skip its own default reset;
   /// return false to let loop() reset the device after WIFI_LOST_RESET_DELAY_S seconds.
   /// </summary>
   /// <returns>True if the WiFi loss was fully handled and no reset is needed.</returns>
   ///
   virtual bool _onWiFiLost()
   {
      return _onWiFiLostCallback != nullptr ? _onWiFiLostCallback() : false;
   }

   ///
   /// <summary>
   /// Returns whether the standard Influx post/flush cycle should run this loop, beyond
   /// the base _usesInflux/_influx->ready() check. Publisher also requires its telemetry
   /// client to be started; Monitor has no extra condition.
   /// </summary>
   ///
   virtual bool _extraInfluxReadyCondition()
   {
      return true;
   }

   ///
   /// <summary>Returns the "async" flag passed to InfluxPoint::post() each loop.</summary>
   ///
   virtual bool _postAsync()
   {
      return false;
   }

   ///
   /// <summary>
   /// OTAUpdateEventHandler implementation, invoked by OTAUpdater just before it downloads
   /// and installs a newly detected firmware version. Logs the update as a single Influx
   /// point before the update starts.
   /// </summary>
   /// <param name="newVersion">The newly detected version string.</param>
   ///
   void onUpdateAvailable(const char* newVersion) override
   {
      std::string otaMessage = std::string("Updating from ") + _config.version + " to " + newVersion;
      _postLogPoint(otaMessage.c_str());
   }

   ///
   /// <summary>
   /// OTAUpdateEventHandler implementation, invoked by OTAUpdater when a detected update
   /// fails to download/install. Logs the failure as a single Influx point.
   /// </summary>
   /// <param name="newVersion">The version that failed to install.</param>
   /// <param name="reason">The error reported by the underlying HTTP update client.</param>
   ///
   void onUpdateFailed(const char* newVersion, const char* reason) override
   {
      std::string otaMessage = std::string("Update to ") + newVersion + " failed: " + reason;
      _postLogPoint(otaMessage.c_str());
   }

   ///
   /// <summary>
   /// OTAUpdateEventHandler implementation, invoked by OTAUpdater when a detected update
   /// downloads and installs successfully, just before the device restarts. Logs the
   /// success as a single Influx point.
   /// </summary>
   /// <param name="newVersion">The version that was successfully installed.</param>
   ///
   void onUpdateSucceeded(const char* newVersion) override
   {
      std::string otaMessage = std::string("Updated to ") + newVersion;
      _postLogPoint(otaMessage.c_str());
   }

   ///
   /// <summary>
   /// Posts a single Influx point to the configured log measurement, tagged with the
   /// sketch name, version (if set), resolved site/location/sensor (whichever are
   /// non-null), and the device's current IP address, with the given message as its
   /// only field.
   /// Does nothing if Influx isn't in use (e.g. begin() hasn't finished setting it up yet).
   /// </summary>
   /// <param name="message">Message to log, both to Influx and Serial.</param>
   ///
   void _postLogPoint(const char* message)
   {
      if (_influx == nullptr)
      {
         return;
      }

      Point point(_config.influx.logMeasurement);
      point.addTag("sketch", _config.sketchName);
      if (_config.version != nullptr)
      {
         point.addTag("version", _config.version);
      }
      if (_site.site != nullptr)
      {
         point.addTag("site", _site.site);
      }
      if (_site.location != nullptr)
      {
         point.addTag("location", _site.location);
      }
      if (_influxSensor != nullptr)
      {
         point.addTag("sensor", _influxSensor);
      }
      point.addTag("ip", WiFi.localIP().toString());
      point.addField("message", message);

      // Log points are one-off writes, not part of the periodic sensor batch, so force
      // an immediate flush rather than letting them sit queued until the sensor batch
      // size (set via setWriteOptions()) happens to be reached.
      bool succeeded = _influx->client()->writePoint(point);
      if (succeeded)
      {
         succeeded = _influx->client()->flushBuffer();
      }

      if (succeeded)
      {
         Serial.print("--- INFLUX LOG: ");
         Serial.println(message);
      }
      else
      {
         Serial.print("--- INFLUX LOG: InfluxDB log write failed: ");
         Serial.println(_influx->client()->getLastErrorMessage());
      }
   }

public:
   ///
   /// <summary>
   /// Creates a SketchBase bound to the given board and configuration. Register
   /// sensors, extra Influx points, and loop hooks afterward, then call begin().
   /// </summary>
   /// <param name="arduino">The board wrapper (used as the status indicator directly if it implements IStatus itself; otherwise its onboard NeoPixel LED is used).</param>
   /// <param name="config">Shared configuration.</param>
   ///
   SketchBase(Arduino* arduino, const SketchConfig& config)
      : _arduino(arduino),
#ifdef ARDUINO_STATUS_SUPPORTED
        _status(arduino),
#else
        _status(&_ownedNeoPixelStatus),
        _ownedNeoPixelStatus(&arduino->neoPixel),
#endif
        _config(config),
        _siteResolver(config.preferencesNamespace),
        _topicResolver(config.preferencesNamespace),
        _sensorTimer(SENSOR_INTERVAL_MS)
   {
      ASSERT(arduino != nullptr);
   }

   ///
   /// <summary>
   /// Registers a sensor to be initialized (via arduino.initSensor()) during begin().
   /// </summary>
   /// <param name="label">Sensor label printed during initialization.</param>
   /// <param name="initFunc">Captureless function that initializes the sensor.</param>
   /// <param name="fatal">If true, a failed init resets the device.</param>
   ///
   void addSensor(const char* label, bool (*initFunc)(), bool fatal = true)
   {
      _sensors.push_back({ label, initFunc, fatal });
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
   /// Creates and registers an additional Influx point (beyond the standard
   /// enclosure/CPU points), posted and flushed alongside them each upload cycle. The
   /// resolved site's "site" and "location" tags are added automatically; only
   /// pass extra tags (e.g. "sensor", "item"). Must be called after begin(), once the
   /// site has been resolved.
   /// </summary>
   /// <param name="measurement">Influx measurement name. Defaults to config.influx.measurement.</param>
   /// <param name="tags">Additional key/value pairs to attach as Influx tags.</param>
   /// <returns>Pointer to the created point, owned by this instance.</returns>
   ///
   InfluxPoint* addPoint(const char* measurement, const std::vector<std::pair<const char*, const char*>>& tags)
   {
      std::vector<std::pair<const char*, const char*>> allTags = { { "site", _site.site }, { "location", _site.location } };
      allTags.insert(allTags.end(), tags.begin(), tags.end());

      InfluxPoint* point = new InfluxPoint(measurement, allTags);
      _points.push_back(point);
      return point;
   }

   ///
   /// <summary>
   /// Overload of addPoint() that uses config.influx.measurement as the measurement name.
   /// </summary>
   /// <param name="tags">Additional key/value pairs to attach as Influx tags.</param>
   /// <returns>Pointer to the created point, owned by this instance.</returns>
   ///
   InfluxPoint* addPoint(const std::vector<std::pair<const char*, const char*>>& tags)
   {
      return addPoint(_config.influx.measurement, tags);
   }

   ///
   /// <summary>
   /// Overload of addPoint() for the common case of a single "sensor" tag.
   /// </summary>
   /// <param name="sensor">Value for the "sensor" tag.</param>
   /// <returns>Pointer to the created point, owned by this instance.</returns>
   ///
   InfluxPoint* addPoint(const char* sensor)
   {
      return addPoint({ { "sensor", sensor } });
   }

   ///
   /// <summary>
   /// Overload of addPoint() for the common single-sensor case, using the resolved
   /// "sensor" tag value (site().sensor if set, otherwise config.influx.context.sensor)
   /// as the "sensor" tag value.
   /// </summary>
   /// <returns>Pointer to the created point, owned by this instance.</returns>
   ///
   InfluxPoint* addPoint()
   {
      return addPoint(_influxSensor);
   }

   ///
   /// <summary>Returns the resolved InfluxDB site (only valid after begin() returns).</summary>
   ///
   const InfluxContext& site() const
   {
      return _site;
   }

   ///
   /// <summary>Returns the resolved telemetry topic (only valid after begin() returns; nullptr if not used).</summary>
   ///
   const char* telemetryTopic() const
   {
      return _telemetryTopic;
   }

   ///
   /// <summary>Returns the Influx service, constructed by begin() (nullptr if Influx isn't in use).</summary>
   ///
   Influx* influx()
   {
      return _influx;
   }

   ///
   /// <summary>
   /// Posts a single Influx point to the configured log measurement, tagged with the
   /// resolved site/location/sensor (whichever are non-null), and with the given message.
   /// Also prints the message to Serial. Does nothing (other than the Serial print) if
   /// Influx isn't in use yet (e.g. called before begin() completes).
   /// </summary>
   /// <param name="message">Message text to log.</param>
   ///
   void logMessage(const char* message)
   {
      _postLogPoint(message);
   }

   ///
   /// <summary>
   /// Runs the canonical initialization sequence: banner, board begin(), force-prompt
   /// window, registered sensor inits, site resolution, WiFi, rebooter, OTA, and InfluxDB
   /// setup (including the standard enclosure/CPU points). Call once from setup(), after
   /// registering sensors/fields/hooks.
   /// </summary>
   ///
   void begin()
   {
      SerialX::begin();

      Serial.println();
      Serial.println();

      _arduino->begin(); // sets up the I2C bus/power rail and the RGB status LED

      _status->setStatus(Status::STARTED);

      _arduino->printInitHeader("Initializing");

      std::string fullSketchLine = _config.sketchName;
      if (_config.version != nullptr)
      {
         fullSketchLine += std::string(" ") + _config.version;
      }

      Serial.print("Sketch... ");
      Serial.println(fullSketchLine.c_str());

#ifdef ARDUINO_DISPLAY_SUPPORTED
      std::string sketchLine = fullSketchLine;

      // Shorten the sketch name (replacing the trailing space with ", ") if the full
      // line doesn't fit in the remaining width of the row, e.g. "Temp..., v2.3".
      int16_t availableWidth = _arduino->width() - _arduino->textWidth("Sketch...");
      if (_config.version != nullptr && _arduino->textWidth(sketchLine.c_str()) > availableWidth)
      {
         std::string suffix = std::string("..., ") + _config.version;
         std::string name = _config.sketchName;
         while (name.length() > 0 && _arduino->textWidth((name + suffix).c_str()) > availableWidth)
         {
            name.pop_back();
         }
         sketchLine = name + suffix;
      }

      _arduino->print("Sketch...", Color::LABEL);
      _arduino->printlnR(sketchLine, Color::VALUE);
#endif

      bool hasSiteTable = _config.influx.prompts.size() > 0;
      bool hasTopicTable = _config.telemetry.prompts.size() > 0;
      bool forcePrompt = (hasSiteTable || hasTopicTable) ? _shouldForcePrompt() : false;

      for (const SensorInit& sensor : _sensors)
      {
         if (!_arduino->initSensor(sensor.label, sensor.initFunc) && sensor.fatal)
         {
            _status->setStatus(Status::FAILED);
            Util::reset(10);
         }
      }

      if (_config.includeCpuTemp)
      {
         _cpuTempSensor.begin();
      }

      if (_config.includeEnclosureTemp)
      {
         _enclosureTempSensor.begin();
      }

      // Resolved before initWifi/Influx so the chosen bucket is known before Influx is
      // constructed.
      if (hasSiteTable)
      {
         _site = _siteResolver.resolve(_arduino->preferences, _status, "Select an influx site (* = default):", _config.influx.measurement, _config.influx.prompts.data(), _config.influx.prompts.size(), forcePrompt);
      }
      else
      {
         _site = _resolveFixedSite();
      }
      _influxSensor = _site.sensor;

      if (hasTopicTable)
      {
         _telemetryTopic = _topicResolver.resolve(_arduino->preferences, _status, "Select a telemetry topic (* = default):", _config.telemetry.prompts.data(), _config.telemetry.prompts.size(), forcePrompt);
      }
      else
      {
         _telemetryTopic = _resolveFixedTelemetryTopic();
      }

      _arduino->initWifi(WIFI_SSID, WIFI_PASSWORD, _status);

      if (_config.enableRebooter)
      {
         _arduino->enableRebooter();
      }

      _usesInflux = _shouldUseInflux(hasSiteTable);
      if (_usesInflux)
      {
         _influx = new Influx(_config.influx.intervalS, _status, INFLUXDB_URL, INFLUXDB_ORG, _site.bucket);
         if (!_influx->begin(_arduino))
         {
            _status->setStatus(Status::FAILED);
            delay(1000); // time for LED to show
            Util::reset();
         }

         std::string influxPath = std::string(_site.bucket != nullptr ? _site.bucket : "") + "/" +
                                  _config.influx.measurement + "/" +
                                  (_influxSensor != nullptr ? _influxSensor : "") + "/" +
                                  (_site.site != nullptr ? _site.site : "") + "/" +
                                  (_site.location != nullptr ? _site.location : "");
         std::string startupMessage = _buildStartupMessage(influxPath);
         if (SerialX::lastShutdownReason().length() > 0)
         {
            startupMessage += std::string(", last shutdown: ") + SerialX::lastShutdownReason().c_str();
         }
         _postLogPoint(startupMessage.c_str());

         if (_config.includeEnclosureTemp)
         {
            InfluxPoint* enclosurePoint = addPoint(_config.influx.measurement, { { "sensor", _influxSensor }, { "item", "Enclosure" } });
            _enclosureTempField = enclosurePoint->addRollingAverageField(_config.influx.rollingSamples, "temperature", INFLUX_DECIMALS);
            _enclosureHumidityField = enclosurePoint->addRollingAverageField(_config.influx.rollingSamples, "humidity", INFLUX_DECIMALS);
         }

         if (_config.includeCpuTemp)
         {
            InfluxPoint* cpuPoint = addPoint(_config.influx.measurement, { { "sensor", _influxSensor }, { "item", "CPU" } });
            _cpuTempField = cpuPoint->addValueField("temperature", INFLUX_DECIMALS);
         }

         _influx->client()->setWriteOptions(WriteOptions().batchSize(_points.size()).bufferSize(2 * _points.size()));
      }

      if (_config.enableOTA)
      {
         _arduino->enableOTA(_config.version, _config.sketchName, _status, OTAUpdater::DEFAULT_CHECK_INTERVAL_SECS, this);
      }

      _afterOTASetup();

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
   /// Runs the canonical per-loop sequence: OTA polling, standard sensor sampling, and
   /// the Influx post/flush cycle. Call once from loop().
   /// </summary>
   ///
   void loop()
   {
      esp_task_wdt_reset();

      if (!_arduino->ensureWiFiConnected() && !_onWiFiLost())
      {
         Util::reset(WIFI_LOST_RESET_DELAY_S);
      }

      _beforeOTACheck();

      _arduino->checkForOTA(); // Drives OTA update checks

      if (_sensorTimer.ready())
      {
         if (_config.includeEnclosureTemp)
         {
            _enclosureTempField->set(_enclosureTempSensor.readTemperatureF());
            _enclosureHumidityField->set(_enclosureTempSensor.readHumidity());
         }
      }

      if (_usesInflux && _influx->ready() && _extraInfluxReadyCondition())
      {
         if (_config.includeCpuTemp)
         {
            _cpuTempField->set(_cpuTempSensor.readTemperatureF());
         }

         for (InfluxPoint* point : _points)
         {
            point->post(_influx->client(), _postAsync());
         }

         // Points above were only queued into the write buffer (see the batchSize set in
         // begin()), so flush now to post them together in a single HTTP request sharing
         // one timestamp.
         if (!_influx->client()->flushBuffer())
         {
            Serial.print("InfluxDB flush failed: ");
            Serial.println(_influx->client()->getLastErrorMessage());
         }
      }
   }
};
