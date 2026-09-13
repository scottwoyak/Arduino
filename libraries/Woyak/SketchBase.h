#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID,
// WIFI_PASSWORD, INFLUXDB_URL, and INFLUXDB_ORG are defined). This mirrors the include
// order already used by Monitor-/Publisher-based sketches.

#include <functional>
#include <vector>

#include "Influx.h"
#include "ESP32TempSensor.h"
#include "SiteConfig.h"
#include "Status.h"
#include "TempSensor.h"
#include "Timer.h"
#include "Util.h"

///
/// <summary>
/// Configuration shared by Monitor and Publisher sketches: sketch identity, the
/// site/location table, InfluxDB cadence settings, Monitor's fixed-site fallback, and
/// Publisher's telemetry settings. Fields that only apply to one sketch type (e.g.
/// fixedSite for Monitor, telemetryTopic/telemetryDecimals/publishIntervalMs for
/// Publisher) are simply left at their default when not used.
/// </summary>
///
/// Fields with a default value below only need to be specified by a sketch if it wants
/// to override that default; use designated initializers and list only the fields that
/// differ, e.g. { .sketchName = "Some_Monitor", .version = VERSION,
/// .preferencesNamespace = "Some_Monitor", .sites = SOME_LOCATIONS,
/// .influxSensor = "Gate", .includeCpuTemp = true }.
///
struct SketchConfig
{
   /// <summary>Sketch name, printed at boot and used as the OTA update identifier.</summary>
   const char* sketchName;

   /// <summary>Sketch version string, printed at boot (if set) and used for OTA update checks. Leave null for a testing sketch that doesn't track a version or use OTA.</summary>
   const char* version = nullptr;

   /// <summary>Preferences (NVS) namespace used to persist the selected site/location. Only needed if sites is non-empty.</summary>
   const char* preferencesNamespace = nullptr;

   /// <summary>Table of selectable InfluxDB site+location entries. Leave empty for a sketch with a single fixed site/location instead of a user-selectable table.</summary>
   SiteTable sites;

   /// <summary>Influx measurement name used for the standard enclosure/CPU points and any points added via addPoint().</summary>
   const char* influxMeasurement = "Sensors";

   /// <summary>Influx measurement name used for the single startup/OTA log point.</summary>
   const char* influxLogMeasurement = "Log";

   /// <summary>Value for the "sensor" tag attached to the standard enclosure/CPU points and the log points. Leave null if the sketch doesn't upload sensor/enclosure values to InfluxDB; the "sensor" tag is then omitted.</summary>
   const char* influxSensor = nullptr;

   /// <summary>How often (in seconds) queued Influx points are posted/flushed.</summary>
   uint16_t influxIntervalS = 60;

   /// <summary>Decimal places used when posting the standard enclosure/CPU fields to InfluxDB.</summary>
   uint8_t influxDecimals = 2;

   /// <summary>Number of samples averaged for the standard rolling-average enclosure temperature/humidity fields.</summary>
   size_t influxRollingSamples = 10;

   /// <summary>How often (in milliseconds) the standard enclosure temperature/humidity sensor is sampled.</summary>
   uint16_t sensorIntervalMs = 100;

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

   /// <summary>Monitor only: fixed InfluxDB site+location entry used when sites is empty (no selection prompt). Ignored if sites is non-empty.</summary>
   SiteConfig fixedSite = { nullptr, INFLUXDB_BUCKET, nullptr, nullptr };

   /// <summary>Publisher only: fixed telemetry topic used when sites is left empty (i.e. the sketch has no selectable site table). Ignored if sites is non-empty.</summary>
   const char* telemetryTopic = nullptr;

   /// <summary>Publisher only: decimal places used when publishing the telemetry value over the WebSocket connection.</summary>
   uint8_t telemetryDecimals = 2;

   /// <summary>Publisher only: how often (in milliseconds) the telemetry value source is read and published. 0 means every loop() iteration.</summary>
   uint16_t publishIntervalMs = 0;
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

   /// <summary>Resolves/persists the chosen SiteConfig entry.</summary>
   SiteResolver _siteResolver;

   /// <summary>The resolved site, populated by begin().</summary>
   SiteConfig _site{};

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

   ///
   /// <summary>
   /// Returns the site to use when the config has no selectable site table (i.e.
   /// config.sites is empty). Monitor returns its fixedSite; Publisher returns a site
   /// built from its fixed telemetryTopic. May also print sketch-specific startup
   /// information (e.g. Publisher prints the telemetry topic).
   /// </summary>
   ///
   virtual SiteConfig _resolveFixedSite() = 0;

   ///
   /// <summary>
   /// Returns whether Influx should be initialized this run. Monitor always uses Influx;
   /// Publisher only does so when a selectable site table resolved a bucket.
   /// </summary>
   /// <param name="hasSiteTable">True if config.sites is non-empty.</param>
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

      Point point(_config.influxLogMeasurement);
      point.addTag("sketch", _config.sketchName);
      if (_config.version != nullptr)
      {
         point.addTag("version", _config.version);
      }
      if (_site.influxSite != nullptr)
      {
         point.addTag("site", _site.influxSite);
      }
      if (_site.influxLocation != nullptr)
      {
         point.addTag("location", _site.influxLocation);
      }
      if (_config.influxSensor != nullptr)
      {
         point.addTag("sensor", _config.influxSensor);
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
        _sensorTimer(config.sensorIntervalMs)
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
   /// Creates and registers an additional Influx point (beyond the standard
   /// enclosure/CPU points), posted and flushed alongside them each upload cycle. The
   /// resolved site's "site" and "location" tags are added automatically; only
   /// pass extra tags (e.g. "sensor", "item"). Must be called after begin(), once the
   /// site has been resolved.
   /// </summary>
   /// <param name="measurement">Influx measurement name.</param>
   /// <param name="tags">Additional key/value pairs to attach as Influx tags.</param>
   /// <returns>Pointer to the created point, owned by this instance.</returns>
   ///
   InfluxPoint* addPoint(const char* measurement, const std::vector<std::pair<const char*, const char*>>& tags = {})
   {
      std::vector<std::pair<const char*, const char*>> allTags = { { "site", _site.influxSite }, { "location", _site.influxLocation } };
      allTags.insert(allTags.end(), tags.begin(), tags.end());

      InfluxPoint* point = new InfluxPoint(measurement, allTags);
      _points.push_back(point);
      return point;
   }

   ///
   /// <summary>Returns the resolved site (only valid after begin() returns).</summary>
   ///
   const SiteConfig& site() const
   {
      return _site;
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

      bool hasSiteTable = _config.sites.count > 0;
      bool forcePrompt = false;

      if (hasSiteTable)
      {
         if (Serial)
         {
            // A Serial monitor is attached (e.g. the board is inside an enclosure and
            // buttonA can't be reached), so automatically offer the prompt. It's still
            // bounded by SiteResolver::PROMPT_TIMEOUT_S, so a false-positive connection
            // (or nobody responding) just falls back to the current/default site.
            forcePrompt = true;
         }
         else
         {
            // No Serial monitor detected - fall back to the buttonA window. buttonA is
            // on GPIO0, a strapping pin: holding it low during power-on/reset puts the
            // chip into UART download mode instead of running the sketch, so it can't
            // be checked during boot. Instead, give the user a short window after boot
            // to press it.
            Serial.println("Press buttonA now to reconfigure the site...");
            forcePrompt = SiteResolver::waitForForcePrompt(_arduino->buttonA, FORCE_PROMPT_WINDOW_MS);
         }
      }

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
         _site = _siteResolver.resolve(_arduino->preferences, _status, "Select a site (* = default):", _config.influxMeasurement, _config.influxSensor, _config.sites.sites, _config.sites.count, forcePrompt);
      }
      else
      {
         _site = _resolveFixedSite();
      }

      _arduino->initWifi(WIFI_SSID, WIFI_PASSWORD, _status);

      if (_config.enableRebooter)
      {
         _arduino->enableRebooter();
      }

      _usesInflux = _shouldUseInflux(hasSiteTable);
      if (_usesInflux)
      {
         _influx = new Influx(_config.influxIntervalS, _status, INFLUXDB_URL, INFLUXDB_ORG, _site.influxBucket);
         if (!_influx->begin(_arduino))
         {
            _status->setStatus(Status::FAILED);
            delay(1000); // time for LED to show
            Util::reset();
         }

         std::string influxPath = std::string(_site.influxBucket != nullptr ? _site.influxBucket : "") + "/" +
                                  _config.influxMeasurement + "/" +
                                  (_config.influxSensor != nullptr ? _config.influxSensor : "") + "/" +
                                  (_site.influxSite != nullptr ? _site.influxSite : "") + "/" +
                                  (_site.influxLocation != nullptr ? _site.influxLocation : "");
         _postLogPoint(_buildStartupMessage(influxPath).c_str());

         if (_config.includeEnclosureTemp)
         {
            InfluxPoint* enclosurePoint = addPoint(_config.influxMeasurement, { { "sensor", _config.influxSensor }, { "item", "Enclosure" } });
            _enclosureTempField = enclosurePoint->addRollingAverageField(_config.influxRollingSamples, "temperature", _config.influxDecimals);
            _enclosureHumidityField = enclosurePoint->addRollingAverageField(_config.influxRollingSamples, "humidity", _config.influxDecimals);
         }

         if (_config.includeCpuTemp)
         {
            InfluxPoint* cpuPoint = addPoint(_config.influxMeasurement, { { "sensor", _config.influxSensor }, { "item", "CPU" } });
            _cpuTempField = cpuPoint->addValueField("temperature", _config.influxDecimals);
         }

         _influx->client()->setWriteOptions(WriteOptions().batchSize(_points.size()).bufferSize(2 * _points.size()));
      }

      if (_config.enableOTA)
      {
         _arduino->enableOTA(_config.version, _config.sketchName, _status, OTAUpdater::DEFAULT_CHECK_INTERVAL_SECS, this);
      }

      _afterOTASetup();

      setCpuFrequencyMhz(_config.cpuFrequencyMhz);
   }

   ///
   /// <summary>
   /// Runs the canonical per-loop sequence: OTA polling, standard sensor sampling, and
   /// the Influx post/flush cycle. Call once from loop().
   /// </summary>
   ///
   void loop()
   {
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
