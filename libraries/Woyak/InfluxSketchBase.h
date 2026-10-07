#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID,
// WIFI_PASSWORD, INFLUXDB_URL, and INFLUXDB_ORG are defined). This mirrors the include
// order already used by MonitorSketch-/PublisherSketch-based sketches.

#include <WiFi.h>
#include <functional>
#include <vector>

#include "Influx.h"
#include "ESP32TempSensor.h"
#include "Logger.h"
#include "SerialX.h"
#include "InfluxConfig.h"
#include "PreferencesResolver.h"
#include "SketchBase.h"
#include "Status.h"
#include "TempSensor.h"
#include "Timer.h"
#include "Util.h"

///
/// <summary>
/// Owns the initialization/loop logic shared by MonitorSketch and PublisherSketch:
/// startup print, force-prompt window, sensor init, site resolution, WiFi, rebooter,
/// OTA, InfluxDB setup (including the single startup/OTA log point and the standard
/// enclosure/CPU points), and the standard per-loop sensor sampling/Influx post cycle.
/// Behavior that differs between MonitorSketch (no telemetry) and PublisherSketch
/// (telemetry WebSocket client) is factored out into the protected virtual hooks below,
/// overridden by each subclass.
/// </summary>
///
class InfluxSketchBase : public SketchBase
{
public:
   /// <summary>How long to wait after boot for a buttonA press before proceeding.</summary>
   static constexpr uint16_t FORCE_PROMPT_WINDOW_MS = 2000;

   /// <summary>Decimal places used when posting the standard enclosure/CPU fields to InfluxDB.</summary>
   static constexpr uint8_t INFLUX_DECIMALS = 2;

   /// <summary>How often (in seconds) the standard enclosure, CPU and WiFi values are sampled.</summary>
   static constexpr uint16_t STANDARD_SAMPLE_INTERVAL_S = 1;

   /// <summary>How often (in seconds) the standard enclosure, CPU and WiFi values are uploaded, as averages (and a minimum for WiFi RSSI) over that period.</summary>
   static constexpr uint16_t STANDARD_UPLOAD_INTERVAL_S = 60;

   /// <summary>Number of samples in each standard rolling window, covering exactly one upload interval.</summary>
   static constexpr size_t NUM_STANDARD_SAMPLES = STANDARD_UPLOAD_INTERVAL_S / STANDARD_SAMPLE_INTERVAL_S;

   /// <summary>Preferences keys for the resolved InfluxDB site entry (bucket/site/location), in InfluxContext field order.</summary>
   static constexpr const char* SITE_KEYS[] = { "bucket", "site", "location" };

   /// <summary>Column metadata for the resolved InfluxDB site entry, matching SITE_KEYS order.</summary>
   static constexpr SerialTable::Column SITE_COLUMNS[] = {
      { "Bucket", 13 },
      { "Site", 11 },
      { "Location", 13 },
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
      return PreferencesResolver::waitForForcePrompt(_arduino->buttonA, FORCE_PROMPT_WINDOW_MS);
   }

   /// <summary>InfluxDB settings, copied from the constructor argument.</summary>
   InfluxConfig _influxConfig;

   /// <summary>Resolves/persists the chosen InfluxDB site entry.</summary>
   PreferencesResolver _siteResolver;

   /// <summary>The resolved InfluxDB site, populated by begin().</summary>
   InfluxContext _site{};

   /// <summary>Owned enclosure temperature/humidity sensor, used if influxConfig.includeEnclosureTemp is true.</summary>
   TempSensor _enclosureTempSensor;

   /// <summary>Constructed by begin(), once the InfluxDB bucket has been resolved. Left nullptr if Influx isn't in use (see _shouldUseInflux()).</summary>
   Influx* _influx = nullptr;

   /// <summary>True if Influx is in use; set by begin() from _shouldUseInflux().</summary>
   bool _usesInflux = false;

   /// <summary>Points registered via addPoint(); posted and flushed together when the sketch calls postPoints().</summary>
   std::vector<InfluxPoint*> _points;

   /// <summary>Standard enclosure/CPU/WiFi points; posted and flushed together every STANDARD_UPLOAD_INTERVAL_S, independent of _points.</summary>
   std::vector<InfluxPoint*> _standardPoints;

   InfluxField* _enclosureTempField = nullptr;
   InfluxField* _enclosureHumidityField = nullptr;
   InfluxField* _cpuTempField = nullptr;
   InfluxField* _rssiField = nullptr;
   InfluxField* _rssiMinField = nullptr;

   /// <summary>Owned CPU temperature sensor, used if influxConfig.includeCpuTemp is true.</summary>
   ESP32TempSensor _cpuTempSensor;

   /// <summary>Paces sampling of the standard enclosure/CPU/WiFi values (STANDARD_SAMPLE_INTERVAL_S).</summary>
   TimerSecs _sensorTimer;

   /// <summary>Paces posting of the standard points (STANDARD_UPLOAD_INTERVAL_S).</summary>
   TimerSecs _standardUploadTimer;

   /// <summary>True once the Influx write buffer has been sized for the final point count (see loop()).</summary>
   bool _writeOptionsApplied = false;

   ///
   /// <summary>
   /// Creates an Influx point
   /// this sketch's name, without registering it for posting.
   /// </summary>
   /// <param name="measurement">Influx measurement name.</param>
   /// <param name="tags">Additional key/value pairs to attach as Influx tags.</param>
   /// <returns>Pointer to the created point, owned by this instance.</returns>
   ///
   InfluxPoint* _createPoint(const char* measurement, const std::vector<std::pair<const char*, const char*>>& tags)
   {
      std::vector<std::pair<const char*, const char*>> allTags = { { "site", _site.site }, { "location", _site.location }, { "sketch", _config.sketchName } };
      allTags.insert(allTags.end(), tags.begin(), tags.end());

      return new InfluxPoint(measurement, allTags);
   }

   ///
   /// <summary>
   /// Creates one of the standard points (enclosure, CPU, WiFi) and registers it in
   /// _standardPoints so it uploads every STANDARD_UPLOAD_INTERVAL_S.
   /// </summary>
   /// <param name="item">Value for the point's "item" tag.</param>
   /// <returns>Pointer to the created point, owned by this instance.</returns>
   ///
   InfluxPoint* _addStandardPoint(const char* item)
   {
      InfluxPoint* point = _createPoint(_influxConfig.measurement, { { "item", item } });
      _standardPoints.push_back(point);
      return point;
   }

   ///
   /// <summary>
   /// Logs a failed InfluxDB flush along with the current WiFi signal strength.
   /// </summary>
   ///
   void _logFlushFailure()
   {
      Logger.log(std::string("InfluxDB flush failed: ") + _influx->client()->getLastErrorMessage().c_str() + std::string(" (WiFi RSSI ") + std::to_string(WiFi.RSSI()) + " dBm)", LogSeverity::ERROR);
   }

   ///
   /// <summary>
   /// Sizes the Influx write buffer for the final point count, once, on first use.
   /// Deferred from begin(): sketch-specific points (added via addPoint() in the
   /// sketch's own setup(), after monitor.begin()/publisher.begin() returns) aren't
   /// registered yet when begin() runs, so sizing the write buffer there would
   /// undercount the points and cause the buffer to wrap and silently drop the
   /// earliest-queued points every cycle.
   /// </summary>
   ///
   void _applyWriteOptions()
   {
      if (_writeOptionsApplied)
      {
         return;
      }

      size_t numPoints = _points.size() + _standardPoints.size();
      _influx->client()->setWriteOptions(WriteOptions().batchSize(numPoints).bufferSize(2 * numPoints));
      _writeOptionsApplied = true;
   }

   ///
   /// <summary>
   /// Posts the given points and flushes them to InfluxDB, logging any flush failure.
   /// </summary>
   /// <param name="points">Points to post.</param>
   ///
   void _postAndFlush(const std::vector<InfluxPoint*>& points)
   {
      if (points.empty())
      {
         return;
      }

      _applyWriteOptions();

      for (InfluxPoint* point : points)
      {
         point->post(_influx->client(), _postAsync());
      }

      // Points above were only queued into the write buffer (see the batchSize set in
      // _applyWriteOptions()), so flush now to post them together in a single HTTP
      // request sharing one timestamp.
      if (!_influx->client()->flushBuffer())
      {
         _logFlushFailure();
      }
   }

   ///
   /// <summary>
   /// Returns the InfluxDB site to use when the config has no selectable influx.prompts
   /// table (i.e. influxConfig.prompts is empty). Monitor returns its influx.context;
   /// Publisher returns an empty site (no Influx bucket) since it has no fixed-site
   /// concept of its own.
   /// </summary>
   ///
   virtual InfluxContext _resolveFixedSite() = 0;

   ///
   /// <summary>
   /// Returns whether the subclass has its own selection to prompt for in begin(), so
   /// the force-prompt window is offered even without an Influx site table. Publisher
   /// returns true when it has a telemetry topic table.
   /// </summary>
   /// <returns>True if the subclass has a selectable prompt.</returns>
   ///
   virtual bool _hasExtraPrompts()
   {
      return false;
   }

   ///
   /// <summary>
   /// Extension point run in begin() right after the Influx site is resolved, before
   /// sensor init and WiFi. Publisher uses this to resolve its telemetry topic.
   /// </summary>
   /// <param name="forcePrompt">If true, subclass prompts should be shown even if a saved selection exists.</param>
   ///
   virtual void _resolveExtra(bool forcePrompt)
   {
   }

   ///
   /// <summary>
   /// Returns whether Influx should be initialized this run. Monitor always uses Influx;
   /// Publisher only does so when a selectable Influx site table resolved a bucket.
   /// </summary>
   /// <param name="hasSiteTable">True if influxConfig.prompts is non-empty.</param>
   ///
   virtual bool _shouldUseInflux(bool hasSiteTable) = 0;

   ///
   /// <summary>
   /// Extension point run after OTA is enabled, at the end of begin(). Publisher uses
   /// this to construct and start its telemetry WebSocket client; Monitor doesn't need
   /// it.
   /// </summary>
   ///
   virtual void _afterOTASetup()
   {}

   ///
   /// <summary>
   /// Extension point run at the very start of loop(), before OTA polling. Publisher
   /// uses this to publish the current value and poll its telemetry client; Monitor
   /// doesn't need it.
   /// </summary>
   ///
   virtual void _beforeOTACheck()
   {}

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

public:
   ///
   /// <summary>
   /// Creates an InfluxSketchBase bound to the given board and configuration. Register
   /// sensors, extra Influx points, and loop hooks afterward, then call begin().
   /// </summary>
   /// <param name="config">Shared configuration.</param>
   /// <param name="influxConfig">InfluxDB settings (site selection, measurement, post cadence, standard enclosure/CPU points).</param>
   ///
   InfluxSketchBase(const SketchConfig& config, const InfluxConfig& influxConfig)
      : SketchBase(config),
      _influxConfig(influxConfig),
      _siteResolver(config.preferencesNamespace, SITE_KEYS),
      _sensorTimer(STANDARD_SAMPLE_INTERVAL_S),
      _standardUploadTimer(STANDARD_UPLOAD_INTERVAL_S)
   {}

   ///
   /// <summary>
   /// Creates and registers an additional Influx point (beyond the standard
   /// enclosure/CPU/WiFi points, which upload on their own schedule), posted and
   /// flushed when the sketch calls postPoints(). The
   /// resolved site's "site" and "location" tags are added automatically; only
   /// pass extra tags (e.g. "item"). Must be called after begin(), once the
   /// site has been resolved.
   /// </summary>
   /// <param name="measurement">Influx measurement name. Defaults to influxConfig.measurement.</param>
   /// <param name="tags">Additional key/value pairs to attach as Influx tags.</param>
   /// <returns>Pointer to the created point, owned by this instance.</returns>
   ///
   InfluxPoint* addPoint(const char* measurement, const std::vector<std::pair<const char*, const char*>>& tags)
   {
      InfluxPoint* point = _createPoint(measurement, tags);
      _points.push_back(point);
      return point;
   }

   ///
   /// <summary>
   /// Posts and flushes all points registered via addPoint(). The sketch owns the timing
   /// of its custom points: call this on the sketch's own upload timer. (The standard
   /// CPU/enclosure/WiFi points are posted automatically by loop().)
   /// </summary>
   ///
   void postPoints()
   {
      if (_usesInflux && _extraInfluxReadyCondition())
      {
         _postAndFlush(_points);
      }
   }

   ///
   /// <summary>
   /// Overload of addPoint() that uses influxConfig.measurement as the measurement name.
   /// </summary>
   /// <param name="tags">Additional key/value pairs to attach as Influx tags.</param>
   /// <returns>Pointer to the created point, owned by this instance.</returns>
   ///
   InfluxPoint* addPoint(const std::vector<std::pair<const char*, const char*>>& tags)
   {
      return addPoint(_influxConfig.measurement, tags);
   }

   ///
   /// <summary>
   /// Overload of addPoint() for the common case of a point with no extra tags beyond
   /// the resolved site's "site" and "location".
   /// </summary>
   /// <returns>Pointer to the created point, owned by this instance.</returns>
   ///
   InfluxPoint* addPoint()
   {
      return addPoint({});
   }

   ///
   /// <summary>Returns the resolved InfluxDB context (only valid after begin() returns).</summary>
   ///
   const InfluxContext& context() const
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
   /// Sends a text log message to the LogServer, and also prints it to Serial.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   ///
   void logMessage(const char* message)
   {
      _logMessage(message);
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

      arduino.begin(); // sets up the I2C bus/power rail and the RGB status LED

      _status->setStatus(Status::STARTED);

      std::string startingMessage = std::string("Starting ") + _config.sketchName;
      if (_config.version != nullptr)
      {
         startingMessage += std::string(" ") + _config.version;
      }
      _logMessage(startingMessage);

      _printStartupInfo();

      bool hasSiteTable = _influxConfig.prompts.size() > 0;
      bool forcePrompt = (hasSiteTable || _hasExtraPrompts()) ? _shouldForcePrompt() : false;

      // Resolved before initWifi/Influx so the chosen bucket is known before Influx is
      // constructed. Resolved here (before the sensor loop) purely so the telemetry/Influx
      // info lines below can be printed/logged right after "Initializing", even though the
      // actual WiFi/Influx connections are still made later, after sensor init.
      if (hasSiteTable)
      {
         std::vector<const char*> flattenedSites;
         for (const InfluxContext& site : _influxConfig.prompts)
         {
            flattenedSites.push_back(site.bucket);
            flattenedSites.push_back(site.site);
            flattenedSites.push_back(site.location);
         }

         String header = String("Select an influx site (measurement=") + _influxConfig.measurement + ", * = default):";
         std::span<const String> resolved = _siteResolver.resolve(_arduino->preferences, _status, header.c_str(), SITE_COLUMNS, flattenedSites.data(), _influxConfig.prompts.size(), forcePrompt);
         // The bucket is always INFLUXDB_BUCKET; a bucket saved to Preferences by an older
         // build (e.g. "Monitor") is ignored.
         _site = { INFLUXDB_BUCKET, resolved[1].c_str(), resolved[2].c_str() };
      }
      else
      {
         _site = _resolveFixedSite();
      }

      _resolveExtra(forcePrompt);

      std::string influxMessage = std::string("Influx:\n") +
         "   bucket=\"" + (_site.bucket != nullptr ? _site.bucket : "") + "\"\n" +
         "   site=\"" + (_site.site != nullptr ? _site.site : "") + "\"\n" +
         "   location=\"" + (_site.location != nullptr ? _site.location : "") + "\"";
      _logMessage(influxMessage);
      if (SerialX::lastShutdownReason().length() > 0)
      {
         _logMessage(std::string("Last shutdown: ") + SerialX::lastShutdownReason().c_str());
      }

      // Connect WiFi and enable OTA (which performs an immediate check) before sensor
      // init, so a fatal sensor failure still leaves a chance to push an OTA fix on
      // every reboot instead of boot-looping before WiFi/OTA is ever reachable.
      _beginConnect();

      // Logging starts as soon as WiFi is up so failures in the remaining init steps are captured
      _beginLoggerConnection(_site.site, _site.location);

      _initSensors();

      if (_influxConfig.includeCpuTemp)
      {
         _cpuTempSensor.begin();
      }

      if (_influxConfig.includeEnclosureTemp)
      {
         _enclosureTempSensor.begin();
      }

      _usesInflux = _shouldUseInflux(hasSiteTable);
      if (_usesInflux)
      {
         new Influx(STANDARD_UPLOAD_INTERVAL_S, _status,
         _logStatusStart("Influx... ");
         if (!_influx->begin(_arduino))
         {
            _logStatusEnd("FAILED");
            _status->setStatus(Status::FAILED);
            delay(1000); // time for LED to show
            Util::reset(0.0f, "Influx failed to begin");
         }

         _logStatusEnd("OK");

         std::string intervalMessage = std::string("Standard values (CPU, enclosure, WiFi) measured every ") + std::to_string(STANDARD_SAMPLE_INTERVAL_S) +
            " s with an average uploaded every " + std::to_string(STANDARD_UPLOAD_INTERVAL_S) + " seconds";
         _logMessage(intervalMessage);

         if (_influxConfig.includeEnclosureTemp)
         {
            InfluxPoint* enclosurePoint = _addStandardPoint("Enclosure");
            _enclosureTempField = enclosurePoint->addRollingAverageField(NUM_STANDARD_SAMPLES, "temperature", INFLUX_DECIMALS);
            _enclosureHumidityField = enclosurePoint->addRollingAverageField(NUM_STANDARD_SAMPLES, "humidity", INFLUX_DECIMALS);
         }

         if (_influxConfig.includeCpuTemp)
         {
            InfluxPoint* cpuPoint = _addStandardPoint("CPU");
            _cpuTempField = cpuPoint->addRollingAverageField(NUM_STANDARD_SAMPLES, "temperature", INFLUX_DECIMALS);
         }

         if (_influxConfig.includeWiFiRssi)
         {
            InfluxPoint* wifiPoint = _addStandardPoint("WiFi");
            _rssiField = wifiPoint->addRollingAverageField(NUM_STANDARD_SAMPLES, "rssi", 0);

            InfluxPoint* wifiMinPoint = _addStandardPoint("WiFi_Min");
            _rssiMinField = wifiMinPoint->addRollingMinField(NUM_STANDARD_SAMPLES, "rssi", 0);
         }
      }

      _afterOTASetup();

      _finishLoggerSetup();
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

      _loopStep();

      if (_sensorTimer.ready())
      {
         if (_influxConfig.includeEnclosureTemp)
         {
            _enclosureTempField->set(_enclosureTempSensor.readTemperatureF());
            _enclosureHumidityField->set(_enclosureTempSensor.readHumidity());
         }

         if (_influxConfig.includeCpuTemp)
         {
            _cpuTempField->set(_cpuTempSensor.readTemperatureF());
         }

         if (_influxConfig.includeWiFiRssi)
         {
            int32_t rssi = WiFi.RSSI();
            _rssiField->set(rssi);
            _rssiMinField->set(rssi);
         }
      }

      if (_usesInflux && _standardUploadTimer.ready() && _extraInfluxReadyCondition())
      {
         _postAndFlush(_standardPoints);
      }
   }
};
