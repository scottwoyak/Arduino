#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID,
// WIFI_PASSWORD, TELEMETRY_HOST, TELEMETRY_PORT, INFLUXDB_URL, and INFLUXDB_ORG are
// defined). This mirrors the include order already used by Gate/Wind/Wave_Publisher.

#include <functional>
#include <vector>

#include "Influx.h"
#include "ESP32TempSensor.h"
#include "SiteConfig.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "TempSensor.h"
#include "Timer.h"
#include "Util.h"

///
/// <summary>
/// Configuration for a Publisher (see Publisher). Holds the knobs shared by every
/// Gate/Wind/Wave-style publisher sketch: sketch identity, the site/location table, and
/// InfluxDB/telemetry cadence settings. Sketch-specific behavior (sensors to
/// initialize, the value streamed over telemetry, extra Influx points, per-loop work)
/// is registered on the Publisher instance separately, before calling begin().
///
/// Fields with a default value below only need to be specified by a sketch if it wants
/// to override that default; use designated initializers and list only the fields that
/// differ, e.g. { .sketchName = "Gate_Publisher", .version = VERSION,
/// .preferencesNamespace = "Gate_Publisher", .sites = GATE_LOCATIONS,
/// .influxSensor = "Gate", .includeCpuTemp = true }.
/// </summary>
///
struct PublisherConfig
{
   /// <summary>Sketch name, printed at boot and used as the OTA update identifier.</summary>
   const char* sketchName;

   /// <summary>Sketch version string, printed at boot (if set) and used for OTA update checks. Leave null for a testing sketch that doesn't track a version or use OTA.</summary>
   const char* version = nullptr;

   /// <summary>Preferences (NVS) namespace used to persist the selected site/location. Only needed if sites is non-empty.</summary>
   const char* preferencesNamespace = nullptr;

   /// <summary>Table of selectable telemetry topic / InfluxDB site+location entries. Leave unset (default-constructed, empty) for a testing sketch with a single fixed telemetry topic and no site prompting; set telemetryTopic instead.</summary>
   SiteTable sites;

   /// <summary>Fixed telemetry topic used when sites is left empty (i.e. the sketch has no selectable site table). Ignored if sites is non-empty.</summary>
   const char* telemetryTopic = nullptr;

   /// <summary>Influx measurement name used for the standard enclosure/CPU points and any points added via addPoint().</summary>
   const char* influxMeasurement = "Sensors";

   /// <summary>Influx measurement name used for the single startup log point (sketch name, version, telemetry topic).</summary>
   const char* influxLogMeasurement = "Log";

   /// <summary>Value for the "sensor" tag attached to the standard enclosure/CPU points and the startup log point. Leave null if the sketch doesn't upload sensor/enclosure values to InfluxDB; the "sensor" tag is then omitted.</summary>
   const char* influxSensor = nullptr;

   /// <summary>How often (in seconds) queued Influx points are posted/flushed.</summary>
   uint16_t influxIntervalS = 60;

   /// <summary>Decimal places used when posting the standard enclosure/CPU fields to InfluxDB.</summary>
   uint8_t influxDecimals = 2;

   /// <summary>Number of samples averaged for the standard rolling-average enclosure temperature/humidity fields.</summary>
   size_t influxRollingSamples = 10;

   /// <summary>Decimal places used when publishing the telemetry value over the WebSocket connection.</summary>
   uint8_t telemetryDecimals = 2;

   /// <summary>How often (in milliseconds) the standard enclosure temperature/humidity sensor is sampled.</summary>
   uint16_t sensorIntervalMs = 100;

   /// <summary>How often (in milliseconds) the telemetry value source is read and published. 0 means every loop() iteration.</summary>
   uint16_t publishIntervalMs = 0;

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
/// Owns the initialization and loop sequence shared by every Gate/Wind/Wave-style
/// publisher sketch: banner, force-prompt window, sensor init, site resolution, WiFi,
/// rebooter, OTA, InfluxDB setup (including a single startup log point with the sketch
/// name, version, and telemetry topic), standard enclosure/CPU points, and the telemetry
/// WebSocket client. A sketch registers its sensors, published value, extra Influx
/// points, and per-loop work via the methods below before calling begin(), then calls
/// begin() once from setup() and loop() once from loop().
/// </summary>
///
class Publisher : private OTAUpdateEventHandler
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

private:
   /// <summary>Board wrapper.</summary>
   Arduino* _arduino;

   /// <summary>Status indicator driving visual feedback during begin()/loop(). Points at the board itself when it implements IStatus (e.g. WaveShare_ESP32_S3_Zero_Sensors's combined external RGB LED/onboard NeoPixel indicator); otherwise falls back to _ownedNeoPixelStatus, driven by the board's onboard NeoPixel LED.</summary>
   IStatus* _status;

#ifndef ARDUINO_STATUS_SUPPORTED
   /// <summary>Fallback status indicator, used when the board doesn't implement IStatus itself.</summary>
   NeoPixelStatus _ownedNeoPixelStatus;
#endif

   /// <summary>Copy of the config passed to the constructor.</summary>
   PublisherConfig _config;

   /// <summary>Resolves/persists the chosen SiteConfig entry.</summary>
   SiteResolver _siteResolver;

   /// <summary>The resolved site, populated by begin().</summary>
   SiteConfig _site{};

   /// <summary>Sensors registered via addSensor(), run in order during begin().</summary>
   std::vector<SensorInit> _sensors;

   /// <summary>Function that produces the value streamed over telemetry.</summary>
   float (*_valueSource)() = nullptr;

   /// <summary>Owned enclosure temperature/humidity sensor, used if config.includeEnclosureTemp is true.</summary>
   TempSensor _enclosureTempSensor;

   /// <summary>Constructed by begin(), once the InfluxDB bucket has been resolved. Left nullptr for telemetry-only sketches (no site table, i.e. config.sites is empty).</summary>
   Influx* _influx = nullptr;

   /// <summary>True if this Publisher uses InfluxDB, i.e. config.sites is non-empty; set by begin().</summary>
   bool _usesInflux = false;

   ///
   /// <summary>
   /// Default telemetry event handler used unless a custom handler is registered via
   /// setTelemetryHandler(). Extends TelemetryEventHandler with a single extra
   /// onStarted() callback slot (see Publisher::setOnStartedCallback()) so a sketch can
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

   /// <summary>Constructed by begin(), once the telemetry topic has been resolved.</summary>
   TelemetryPublisher* _client = nullptr;

   /// <summary>Points registered via addPoint() (including the standard enclosure/CPU
   /// points below); posted and flushed together each Influx upload cycle.</summary>
   std::vector<InfluxPoint*> _points;

   InfluxField* _enclosureTempField = nullptr;
   InfluxField* _enclosureHumidityField = nullptr;
   InfluxField* _cpuTempField = nullptr;

   /// <summary>Owned CPU temperature sensor, used if config.includeCpuTemp is true.</summary>
   ESP32TempSensor _cpuTempSensor;

   Timer _sensorTimer;
   Timer _publishTimer;

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
      _postLogPoint({ { "sketch", _config.sketchName }, { "event", "OTA Update" }, { "currentVersion", _config.version }, { "newVersion", newVersion } },
                     otaMessage.c_str());
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
      _postLogPoint({ { "sketch", _config.sketchName }, { "event", "OTA Update Failed" }, { "currentVersion", _config.version }, { "newVersion", newVersion }, { "reason", reason } },
                     otaMessage.c_str());
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
      _postLogPoint({ { "sketch", _config.sketchName }, { "event", "OTA Update Succeeded" }, { "currentVersion", _config.version }, { "newVersion", newVersion } },
                     otaMessage.c_str());
   }

   ///
   /// <summary>
   /// Posts a single Influx point to the configured log measurement, tagged with the
   /// resolved site/location/sensor (whichever are non-null), and with the given fields.
   /// Does nothing if Influx isn't in use (e.g. begin() hasn't finished setting it up yet).
   /// </summary>
   /// <param name="fields">Field name/value pairs to attach to the log point.</param>
   /// <param name="message">Serial message printed on success, before the comma-separated fields. Defaults to "Influx log" if not given.</param>
   ///
   void _postLogPoint(const std::vector<std::pair<const char*, const char*>>& fields, const char* message = "Influx log")
   {
      if (_influx == nullptr)
      {
         return;
      }

      Point point(_config.influxLogMeasurement);
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
      for (const auto& field : fields)
      {
         point.addField(field.first, field.second);
      }
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
         Serial.print(message);
         Serial.print(": ");
         for (size_t i = 0; i < fields.size(); i++)
         {
            if (i > 0)
            {
               Serial.print(", ");
            }
            Serial.print(fields[i].first);
            Serial.print("=");
            Serial.print(fields[i].second);
         }
         Serial.println();
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
   /// Creates a Publisher bound to the given board and configuration. Register sensors,
   /// the value source, extra Influx points, and loop hooks afterward, then call begin().
   /// </summary>
   /// <param name="arduino">The board wrapper (used as the status indicator directly if it implements IStatus itself; otherwise its onboard NeoPixel LED is used).</param>
   /// <param name="config">Shared publisher configuration.</param>
   ///
   Publisher(Arduino* arduino, const PublisherConfig& config)
      : _arduino(arduino),
#ifdef ARDUINO_STATUS_SUPPORTED
        _status(arduino),
#else
        _status(&_ownedNeoPixelStatus),
        _ownedNeoPixelStatus(&arduino->neoPixel),
#endif
        _config(config),
        _siteResolver(config.preferencesNamespace),
#ifdef ARDUINO_DISPLAY_SUPPORTED
        _telemetryHandler(_status, arduino),
#else
        _telemetryHandler(_status),
#endif
        _sensorTimer(config.sensorIntervalMs),
        _publishTimer(config.publishIntervalMs)
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
   /// base-class behavior Publisher relies on. Ignored if a custom handler is
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
   /// <summary>
   /// Creates and registers an additional Influx point (beyond the standard
   /// enclosure/CPU points), posted and flushed alongside them each upload cycle. The
   /// resolved site's "site" and "location" tags are added automatically; only
   /// pass extra tags (e.g. "sensor", "item"). Must be called after begin(), once the
   /// site has been resolved.
   /// </summary>
   /// <param name="measurement">Influx measurement name.</param>
   /// <param name="tags">Additional key/value pairs to attach as Influx tags.</param>
   /// <returns>Pointer to the created point, owned by this Publisher.</returns>
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
   /// <summary>Returns the Influx service, constructed by begin().</summary>
   ///
   Influx* influx()
   {
      return _influx;
   }

   ///
   /// <summary>Returns the telemetry client, constructed by begin().</summary>
   ///
   TelemetryPublisher* client()
   {
      return _client;
   }

   ///
   /// <summary>
   /// Runs the canonical initialization sequence: banner, board begin(), force-prompt
   /// window, registered sensor inits, site resolution, WiFi, rebooter, OTA, InfluxDB
   /// setup (including the standard enclosure/CPU points), and the telemetry WebSocket
   /// client. Call once from setup(), after registering sensors/fields/hooks.
   /// </summary>
   ///
   void begin()
   {
      SerialX::begin();

      Serial.print(_config.sketchName);
      Serial.print(" ");
      Serial.println(_config.version);

      _arduino->begin(); // sets up the I2C bus/power rail and the RGB status LED

#ifdef ARDUINO_DISPLAY_SUPPORTED
      Influx::startInit(_arduino);
      _arduino->print("Sketch...", Color::LABEL);
      _arduino->printlnR(_config.sketchName, Color::VALUE2);
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
         _site = { _config.telemetryTopic, nullptr, nullptr, nullptr };
      }

      if (!hasSiteTable)
      {
         Serial.print("Telemetry Topic: ");
         Serial.println(_site.telemetryTopic);
      }

      _arduino->initWifi(WIFI_SSID, WIFI_PASSWORD, _status);

      if (_config.enableRebooter)
      {
         _arduino->enableRebooter();
      }

      _usesInflux = hasSiteTable;
      if (_usesInflux)
      {
         _influx = new Influx(_config.influxIntervalS, _status, INFLUXDB_URL, INFLUXDB_ORG, _site.influxBucket);
         if (!_influx->begin(_arduino))
         {
            _status->setStatus(Status::FAILED);
            delay(1000); // time for LED to show
            Util::reset();
         }

         // Log the sketch name, version, telemetry topic, and Influx bucket/site path as
         // a single startup point.
         std::string startupMessage = std::string("Starting \"") + _config.sketchName + "\"";
         std::string influxPath = std::string(_site.influxBucket != nullptr ? _site.influxBucket : "") + "/" +
                                  _config.influxMeasurement + "/" +
                                  (_config.influxSensor != nullptr ? _config.influxSensor : "") + "/" +
                                  (_site.influxSite != nullptr ? _site.influxSite : "") + "/" +
                                  (_site.influxLocation != nullptr ? _site.influxLocation : "");
         _postLogPoint({ { "version", _config.version }, { "telemetryTopic", _site.telemetryTopic }, { "influx", influxPath.c_str() } },
                        startupMessage.c_str());

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
         _arduino->enableOTA(_config.version, _config.sketchName, this);
      }

      _client = new TelemetryPublisher(_site.telemetryTopic, _config.telemetryDecimals, _status, _customTelemetryHandler != nullptr ? _customTelemetryHandler : &_telemetryHandler);

      // Not followed by Influx::endInit() - the initialization display (WiFi, Time, Influx,
      // and now Telemetry rows) is left on-screen so the async connection's OK/FAILED result
      // (printed by TelemetryEventHandler) stays visible. Sketches that show a different UI
      // once connected (e.g. via client->isStarted()) are responsible for clearing the display
      // themselves at that point.
      _arduino->initClient("Telemetry", [this]() { _client->beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, _status);

      setCpuFrequencyMhz(_config.cpuFrequencyMhz);
   }

   ///
   /// <summary>
   /// Runs the canonical per-loop sequence: telemetry value publish, telemetry/OTA
   /// polling, standard sensor sampling, and the Influx post/flush cycle. Call once
   /// from loop().
   /// </summary>
   ///
   void loop()
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
      _arduino->checkForOTA(); // Drives OTA update checks

      if (_sensorTimer.ready())
      {
         if (_config.includeEnclosureTemp)
         {
            _enclosureTempField->set(_enclosureTempSensor.readTemperatureF());
            _enclosureHumidityField->set(_enclosureTempSensor.readHumidity());
         }
      }

      if (_client->isStarted() && _usesInflux && _influx->ready())
      {
         if (_config.includeCpuTemp)
         {
            _cpuTempField->set(_cpuTempSensor.readTemperatureF());
         }

         for (InfluxPoint* point : _points)
         {
            point->post(_influx->client(), true);
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


