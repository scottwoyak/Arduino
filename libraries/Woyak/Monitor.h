#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID,
// WIFI_PASSWORD, INFLUXDB_URL, and INFLUXDB_ORG are defined). This mirrors the include
// order already used by Publisher-based sketches (see Publisher.h).

#include "SketchBase.h"
#include "SerialX.h"

///
/// <summary>
/// Owns the initialization and loop sequence shared by every InfluxDB-only monitor
/// sketch: banner, force-prompt window, sensor init, site resolution, WiFi, rebooter,
/// OTA, InfluxDB setup (including a single startup log point with the sketch name,
/// version, and Influx path), and the standard enclosure/CPU points. Unlike Publisher,
/// Monitor doesn't stream any value over a telemetry WebSocket connection. A sketch
/// registers its sensors, extra Influx points, and per-loop work via the methods below
/// before calling begin(), then calls begin() once from setup() and loop() once from
/// loop(). Shared lifecycle logic lives in SketchBase; this class only supplies the
/// Monitor-specific hook overrides.
///
/// Site resolution defaults to a single fixed site/location (config.influx.context). If
/// config.influx.promptForContext is set instead, Monitor prompts over Serial (or loads the
/// saved values from Preferences) for a bucket (chosen from the shared BUCKET_OPTIONS
/// list) and a site (chosen from the shared SITE_OPTIONS list) and location (entered as
/// free text), persisting them to Preferences under config.preferencesNamespace. On
/// boards with a button (ARDUINO_BUTTON_A_SUPPORTED), gives the user a short
/// buttonA-held window right after boot to force a re-prompt; on boards without a
/// button, automatically offers the re-prompt instead whenever a Serial monitor is
/// attached at boot (e.g. the board is inside an enclosure).
/// </summary>
///
class Monitor : public SketchBase
{
private:
   static constexpr auto SITE_KEY = "site";
   static constexpr auto LOCATION_KEY = "location";
   static constexpr auto BUCKET_KEY = "bucket";

   /// <summary>Bucket choices offered to every Monitor sketch that opts into bucket/site/location prompting (see config.influx.promptForContext).</summary>
   static constexpr const char* BUCKET_OPTIONS[] = { "Monitor", "Testing" };

   /// <summary>Site choices offered to every Monitor sketch that opts into bucket/site/location prompting (see config.influx.promptForContext).</summary>
   static constexpr const char* SITE_OPTIONS[] = { "Bragg", "Lake" };

   String _siteName;
   String _locationName;
   String _bucketName;

   ///
   /// <summary>
   /// Prompts the user over Serial to pick an InfluxDB bucket from BUCKET_OPTIONS.
   /// Blocks until a valid selection is entered.
   /// </summary>
   /// <returns>The chosen bucket name.</returns>
   ///
   String _promptForBucket()
   {
      Serial.println("Select an InfluxDB bucket:");
      for (size_t i = 0; i < std::size(BUCKET_OPTIONS); i++)
      {
         Serial.print("  ");
         Serial.print(i + 1);
         Serial.print(": ");
         Serial.println(BUCKET_OPTIONS[i]);
      }

      String label = "Enter selection (1-" + String(std::size(BUCKET_OPTIONS)) + "): ";
      long selection = SerialX::promptForInt(label, 1, (long)std::size(BUCKET_OPTIONS));
      return BUCKET_OPTIONS[selection - 1];
   }

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
   /// Prompts the user over Serial for a site and location, storing them into _siteName
   /// and _locationName. The site is chosen from SITE_OPTIONS. Blocks until both are
   /// entered/selected non-empty.
   /// </summary>
   ///
   void _promptForSiteLocation()
   {
      Serial.println("Configure this device's site/location:");

      _siteName = _promptForSite();

      do
      {
         _locationName = SerialX::prompt("Enter location: ");
      } while (_locationName.length() == 0);
   }

   ///
   /// <summary>
   /// Asks the user, over Serial, whether to keep the currently loaded saved
   /// bucket/site/location or enter new values instead. Call only after
   /// _loadSavedConfig() has populated _bucketName/_siteName/_locationName. Waits up to
   /// PROMPT_TIMEOUT_S seconds for a response, defaulting to "keep saved" if none arrives.
   /// </summary>
   /// <returns>True if the saved configuration should be kept; false to reconfigure.</returns>
   ///
   bool _promptKeepSavedConfig()
   {
      static constexpr uint16_t PROMPT_TIMEOUT_S = 10;

      String options[] = {
         "Keep saved: " + _bucketName + "/" + _siteName + "/" + _locationName,
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
   /// Prompts the user over Serial for the bucket, site, and location, then saves the
   /// entered/selected values to Preferences for next time.
   /// </summary>
   ///
   void _promptAndSaveSiteLocation()
   {
      _bucketName = _promptForBucket();
      _promptForSiteLocation();

      _arduino->preferences.begin(_config.preferencesNamespace, false);
      _arduino->preferences.putString(SITE_KEY, _siteName);
      _arduino->preferences.putString(LOCATION_KEY, _locationName);
      _arduino->preferences.putString(BUCKET_KEY, _bucketName);
      _arduino->preferences.end();
   }

   ///
   /// <summary>
   /// Checks whether this device's site/location/bucket have been saved to Preferences.
   /// </summary>
   /// <returns>True if a saved configuration exists; otherwise false.</returns>
   ///
   bool _hasSavedConfig()
   {
      _arduino->preferences.begin(_config.preferencesNamespace, true);
      bool hasSavedConfig = _arduino->preferences.isKey(SITE_KEY) && _arduino->preferences.isKey(LOCATION_KEY) && _arduino->preferences.isKey(BUCKET_KEY);
      _arduino->preferences.end();

      return hasSavedConfig;
   }

   ///
   /// <summary>
   /// Loads this device's saved site/location/bucket from Preferences into _siteName,
   /// _locationName, and _bucketName. Only call when _hasSavedConfig() is true.
   /// </summary>
   ///
   void _loadSavedConfig()
   {
      _arduino->preferences.begin(_config.preferencesNamespace, true);
      _siteName = _arduino->preferences.getString(SITE_KEY);
      _locationName = _arduino->preferences.getString(LOCATION_KEY);
      _bucketName = _arduino->preferences.getString(BUCKET_KEY);
      _arduino->preferences.end();
   }

protected:
   ///
   /// <summary>
   /// Returns config.influx.context unless config.influx.promptForContext is set, in which
   /// case a bucket/site/location is prompted for over Serial (or loaded from
   /// Preferences) instead.
   /// </summary>
   ///
   InfluxContext _resolveFixedSite() override
   {
      if (!_config.influx.promptForContext)
      {
         return _config.influx.context;
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
      _printAndLogStatus("Location... ", siteLocation().c_str());

      return InfluxContext{ _bucketName.c_str(), _siteName.c_str(), _locationName.c_str(), _config.influx.context.sensor };
   }

   ///
   /// <summary>Monitor always uses Influx, regardless of whether a site table was configured.</summary>
   ///
   bool _shouldUseInflux(bool /*hasSiteTable*/) override
   {
      return true;
   }

   ///
   /// <summary>Builds the second startup log message: Influx bucket/site details.</summary>
   ///
   std::string _buildStartupMessage(const std::string& influxInfo) override
   {
      return std::string("Influx: ") + influxInfo;
   }

public:
   ///
   /// <summary>
   /// Creates a Monitor bound to the given board and configuration. Register sensors,
   /// extra Influx points, and loop hooks afterward, then call begin().
   /// </summary>
   /// <param name="arduino">The board wrapper (used as the status indicator directly if it implements IStatus itself; otherwise its onboard NeoPixel LED is used).</param>
   /// <param name="config">Shared monitor configuration.</param>
   ///
   Monitor(Arduino* arduino, const SketchConfig& config)
      : SketchBase(arduino, config)
   {
   }

   ///
   /// <summary>
   /// Formats this device's site and location as "Site/Location". Only valid once
   /// begin() has resolved the site (i.e. config.influx.promptForContext was set).
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
   /// config.influx.sensorFailureResetDelayS delay Monitor uses internally for its own
   /// fatal init failures.
   /// </summary>
   ///
   void reportSensorFailure()
   {
      _status->setStatus(Status::FAILED);
      Util::reset(_config.influx.sensorFailureResetDelayS);
   }
};
