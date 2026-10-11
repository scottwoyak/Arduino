#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID,
// WIFI_PASSWORD, INFLUXDB_URL, and INFLUXDB_ORG are defined). This mirrors the include
// order already used by Publisher-based sketches (see PublisherSketch.h).

#include "InfluxSketchBase.h"
#include "SerialX.h"

///
/// <summary>
/// Owns the initialization and loop sequence shared by every InfluxDB-only monitor
/// sketch: startup print, force-prompt window, sensor init, site resolution, WiFi,
/// rebooter, OTA, InfluxDB setup (including a single startup log point with the sketch
/// name, version, and Influx path), and the standard enclosure/CPU points. Unlike
/// PublisherSketch, MonitorSketch doesn't stream any value over a telemetry WebSocket
/// connection. A sketch registers its sensors, extra Influx points, and per-loop work
/// via the methods below before calling begin(), then calls begin() once from setup()
/// and loop() once from loop(). Shared lifecycle logic lives in InfluxSketchBase; this
/// class only supplies the Monitor-specific hook overrides.
/// </summary>
///
/// Site resolution defaults to a single fixed site/location (influxConfig.context). If
/// influxConfig.promptForContext is set instead, MonitorSketch prompts over Serial (or
/// loads the saved values from Preferences) for a site (chosen from the shared
/// SITE_OPTIONS list) and a
/// location (entered as free text), persisting them to Preferences under
/// config.preferencesNamespace. On boards with a button (ARDUINO_BUTTON_A_SUPPORTED),
/// gives the user a short buttonA-held window right after boot to force a re-prompt; on
/// boards without a button, automatically offers the re-prompt instead whenever a
/// Serial monitor is attached at boot (e.g. the board is inside an enclosure).
///
class MonitorSketch : public InfluxSketchBase
{
private:
   static constexpr auto SITE_KEY = "site";
   static constexpr auto LOCATION_KEY = "location";
   static constexpr auto BUCKET_KEY = "bucket";

   /// <summary>Site choices offered to every Monitor sketch that opts into site/location prompting (see influxConfig.promptForContext).</summary>
   static constexpr const char* SITE_OPTIONS[] = { "Bragg", "Lake" };

   /// <summary>Bucket choices (display label -> Influx bucket name) offered when influxConfig.promptForBucket is set.</summary>
   static constexpr const char* BUCKET_LABELS[] = { "30 Day", "7 Day" };
   static constexpr const char* BUCKET_NAMES[] = { INFLUXDB_BUCKET, INFLUXDB_BUCKET_7_DAY };

   String _siteName;
   String _locationName;
   std::string _bucketName = INFLUXDB_BUCKET;

   ///
   /// <summary>
   /// Prompts the user over Serial to pick a site from SITE_OPTIONS. Blocks until a
   /// valid selection is entered.
   /// </summary>
   /// <returns>The chosen site name.</returns>
   ///
   String _promptForSite()
   {
      Serial.println("Select a site:");
      for (size_t i = 0; i < std::size(SITE_OPTIONS); i++)
      {
         Serial.print("  ");
         Serial.print(i + 1);
         Serial.print(": ");
         Serial.println(SITE_OPTIONS[i]);
      }

      String label = "Enter selection (1-" + String(std::size(SITE_OPTIONS)) + "): ";
      long selection = SerialX::promptForInt(label, 1, (long)std::size(SITE_OPTIONS));
      return SITE_OPTIONS[selection - 1];
   }

   ///
   /// <summary>
   /// Prompts the user over Serial to pick a combined Influx bucket + site from a
   /// flattened list of BUCKET_LABELS x SITE_OPTIONS pairs (one prompt instead of two
   /// separate bucket and site prompts), formatted as "Bucket=<label>    Site=<site>"
   /// with the Site= column aligned across rows. Blocks until a valid selection is
   /// entered. Stores the chosen bucket into _bucketName and the chosen site into
   /// _siteName.
   /// </summary>
   ///
   void _promptForSiteAndBucket()
   {
      constexpr size_t NUM_COMBINATIONS = std::size(BUCKET_LABELS) * std::size(SITE_OPTIONS);

      size_t maxBucketLabelLen = 0;
      for (size_t b = 0; b < std::size(BUCKET_LABELS); b++)
      {
         maxBucketLabelLen = max(maxBucketLabelLen, strlen(BUCKET_LABELS[b]));
      }

      Serial.println("Select a bucket/site:");
      size_t index = 0;
      for (size_t b = 0; b < std::size(BUCKET_LABELS); b++)
      {
         String bucketField = String("Bucket=") + BUCKET_LABELS[b];
         while (bucketField.length() < strlen("Bucket=") + maxBucketLabelLen)
         {
            bucketField += " ";
         }

         for (size_t s = 0; s < std::size(SITE_OPTIONS); s++)
         {
            Serial.print("  ");
            Serial.print(index + 1);
            Serial.print(": ");
            Serial.print(bucketField);
            Serial.print("    Site=");
            Serial.println(SITE_OPTIONS[s]);
            index++;
         }
      }

      String label = "Enter selection (1-" + String(NUM_COMBINATIONS) + "): ";
      long selection = SerialX::promptForInt(label, 1, (long)NUM_COMBINATIONS) - 1;

      _bucketName = BUCKET_NAMES[selection / std::size(SITE_OPTIONS)];
      _siteName = SITE_OPTIONS[selection % std::size(SITE_OPTIONS)];
   }

   ///
   /// <summary>
   /// Prompts the user over Serial for a site and location, storing them into _siteName
   /// and _locationName. If influxConfig.promptForBucket is set, the site and bucket are
   /// chosen together from a single combined prompt (see _promptForSiteAndBucket());
   /// otherwise the site alone is chosen from SITE_OPTIONS. If _locationName already
   /// holds a previously saved value (i.e. this is a reconfigure of an existing saved
   /// config), entering a blank line (or spaces only) keeps that previous value instead
   /// of requiring it to be retyped. Blocks until both the site and location are
   /// entered/selected non-empty.
   /// </summary>
   ///
   void _promptForSiteLocation()
   {
      if (_influxConfig.promptForBucket)
      {
         _promptForSiteAndBucket();
      }
      else
      {
         Serial.println("Configure this device's site/location:");
         _siteName = _promptForSite();
      }

      String previousLocation = _locationName;
      String prompt = previousLocation.length() > 0
         ? "Enter location (blank to keep \"" + previousLocation + "\"): "
         : "Enter location: ";

      do
      {
         _locationName = SerialX::prompt(prompt);
         _locationName.trim();
         if (_locationName.length() == 0 && previousLocation.length() > 0)
         {
            _locationName = previousLocation;
         }
      } while (_locationName.length() == 0);
   }

   ///
   /// <summary>
   /// Asks the user, over Serial, whether to keep the currently loaded saved
   /// site/location or enter new values instead. Call only after
   /// _loadSavedConfig() has populated _siteName/_locationName. Waits up to
   /// PROMPT_TIMEOUT_S seconds for a response, defaulting to "keep saved" if none arrives.
   /// </summary>
   /// <returns>True if the saved configuration should be kept; false to reconfigure.</returns>
   ///
   bool _promptKeepSavedConfig()
   {
      static constexpr uint16_t PROMPT_TIMEOUT_S = 10;

      String savedLabel = "Keep saved: " + _siteName + "/" + _locationName;
      if (_influxConfig.promptForBucket)
      {
         savedLabel += String(" (") + _bucketName.c_str() + ")";
      }

      String options[] = {
         savedLabel,
         "Enter new values",
      };

      Serial.println("Reconfigure this device's site/location:");
      for (size_t i = 0; i < std::size(options); i++)
      {
         Serial.print("  ");
         Serial.print(i + 1);
         Serial.print(": ");
         Serial.println(options[i]);
      }

      size_t selection = SerialX::readSelectionWithTimeout(std::size(options), 0, PROMPT_TIMEOUT_S * 1000UL);
      return selection == 0;
   }

   ///
   /// <summary>
   /// Prompts the user over Serial for the site and location, then saves the
   /// entered/selected values to Preferences for next time.
   /// </summary>
   ///
   void _promptAndSaveSiteLocation()
   {
      _promptForSiteLocation();

      _arduino->preferences.begin(_config.preferencesNamespace, false);
      _arduino->preferences.putString(SITE_KEY, _siteName);
      _arduino->preferences.putString(LOCATION_KEY, _locationName);
      if (_influxConfig.promptForBucket)
      {
         _arduino->preferences.putString(BUCKET_KEY, _bucketName.c_str());
      }
      _arduino->preferences.end();
   }

   ///
   /// <summary>
   /// Checks whether this device's site/location have been saved to Preferences.
   /// </summary>
   /// <returns>True if a saved configuration exists; otherwise false.</returns>
   ///
   bool _hasSavedConfig()
   {
      _arduino->preferences.begin(_config.preferencesNamespace, true);
      bool hasSavedConfig = _arduino->preferences.isKey(SITE_KEY) && _arduino->preferences.isKey(LOCATION_KEY);
      _arduino->preferences.end();

      return hasSavedConfig;
   }

   ///
   /// <summary>
   /// Loads this device's saved site/location from Preferences into _siteName
   /// and _locationName.
   /// </summary>
   ///
   void _loadSavedConfig()
   {
      _arduino->preferences.begin(_config.preferencesNamespace, true);
      _siteName = _arduino->preferences.getString(SITE_KEY);
      _locationName = _arduino->preferences.getString(LOCATION_KEY);
      if (_influxConfig.promptForBucket)
      {
         _bucketName = _arduino->preferences.getString(BUCKET_KEY, INFLUXDB_BUCKET).c_str();
      }
      _arduino->preferences.end();
   }

protected:
   ///
   /// <summary>
   /// Returns influxConfig.context unless influxConfig.promptForContext is set, in which
   /// case a site/location is prompted for over Serial (or loaded from
   /// Preferences) instead.
   /// </summary>
   ///
   InfluxContext _resolveFixedSite() override
   {
      if (!_influxConfig.promptForContext)
      {
         return _influxConfig.context;
      }

      bool hasSavedConfig = _hasSavedConfig();
      bool reconfigure = _shouldForcePrompt();

      if (reconfigure && hasSavedConfig)
      {
         _loadSavedConfig();
         reconfigure = !_promptKeepSavedConfig();
      }

      if (reconfigure || !hasSavedConfig)
      {
         _promptAndSaveSiteLocation();
      }
      else
      {
         _loadSavedConfig();
      }
      std::string locationStatus = siteLocation();
      if (_influxConfig.promptForBucket)
      {
         locationStatus += " (" + _bucketName + ")";
      }
      _printAndLogStatus("Location... ", locationStatus.c_str());

      return InfluxContext{ _bucketName.c_str(), _siteName.c_str(), _locationName.c_str() };
   }

   ///
   /// <summary>Monitor always uses Influx, regardless of whether a site table was configured.</summary>
   ///
   bool _shouldUseInflux(bool /*hasSiteTable*/) override
   {
      return true;
   }

public:
   ///
   /// <summary>
    /// Creates a MonitorSketch bound to the given board and configuration. Register
    /// sensors, extra Influx points, and loop hooks afterward, then call begin().
    /// </summary>
   /// <param name="config">Shared configuration.</param>
   /// <param name="influxConfig">InfluxDB settings.</param>
   ///
   MonitorSketch(const SketchConfig& config, const InfluxConfig& influxConfig)
      : InfluxSketchBase(config, influxConfig)
   {
   }

   ///
   /// <summary>
   /// Formats this device's site and location as "Site/Location". Only valid once
   /// begin() has resolved the site (i.e. influxConfig.promptForContext was set).
   /// </summary>
   /// <returns>The formatted "Site/Location" string.</returns>
   ///
   std::string siteLocation() const
   {
      return std::string(_siteName.c_str()) + "/" + _locationName.c_str();
   }

   ///
   /// <summary>
   /// Signals a fatal sensor initialization failure using the same status indicator and
    /// config.sensorFailureResetDelayS delay MonitorSketch uses internally for its
    /// own fatal init failures.
   /// </summary>
   ///
   void reportSensorFailure()
   {
      _status->setStatus(Status::FAILED);
      Util::reset(_config.sensorFailureResetDelayS, "Sensor failure reported");
   }
};
