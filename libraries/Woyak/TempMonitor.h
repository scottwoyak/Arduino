#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), WiFiSettings.h (so WIFI_SSID, WIFI_PASSWORD,
// INFLUXDB_URL, and INFLUXDB_ORG are defined), and Monitor.h. This mirrors Monitor.h's
// own include-order requirement.

#include "Monitor.h"
#include "SerialX.h"

///
/// <summary>
/// Monitor subclass that resolves a free-text site/location/bucket instead of picking
/// from a fixed SiteConfig table: prompts over Serial (or loads the saved values from
/// Preferences) during begin(). On boards with a button (ARDUINO_BUTTON_A_SUPPORTED),
/// gives the user a short buttonA-held window right after boot to force a re-prompt;
/// on boards without a button, automatically offers the re-prompt instead whenever a
/// Serial monitor is attached at boot (e.g. the board is inside an enclosure). Also
/// exposes reportSensorFailure() so a sketch's setup() can signal a fatal sensor init
/// failure using the same status indicator/reset path Monitor uses internally.
/// </summary>
///
class TempMonitor : public Monitor
{
private:
   static constexpr uint16_t RECONFIGURE_PROMPT_WINDOW_MS = 2000;
   static constexpr auto SITE_KEY = "site";
   static constexpr auto LOCATION_KEY = "location";
   static constexpr auto BUCKET_KEY = "bucket";

   const char* _preferencesNamespace;
   const char* const* _bucketOptions;
   size_t _numBucketOptions;
   uint8_t _resetDelayS;

   String _siteName;
   String _locationName;
   String _bucketName;

   ///
   /// <summary>
   /// Prompts the user over Serial to pick an InfluxDB bucket from the configured bucket
   /// options. Blocks until a valid selection is entered.
   /// </summary>
   /// <returns>The chosen bucket name.</returns>
   ///
   String _promptForBucket()
   {
      Serial.println("Select an InfluxDB bucket:");
      for (size_t i = 0; i < _numBucketOptions; i++)
      {
         Serial.print("  ");
         Serial.print(i + 1);
         Serial.print(": ");
         Serial.println(_bucketOptions[i]);
      }

      String label = "Enter selection (1-" + String(_numBucketOptions) + "): ";
      long selection = SerialX::promptForInt(label, 1, (long)_numBucketOptions);
      return _bucketOptions[selection - 1];
   }

   ///
   /// <summary>
   /// Prompts the user over Serial for a free-text site and location, storing them into
   /// _siteName and _locationName. Blocks until both are entered non-empty.
   /// </summary>
   ///
   void _promptForSiteLocation()
   {
      Serial.println("Configure this device's site/location:");

      do
      {
         _siteName = SerialX::prompt("Enter site: ");
      } while (_siteName.length() == 0);

      do
      {
         _locationName = SerialX::prompt("Enter location: ");
      } while (_locationName.length() == 0);
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

      _arduino->preferences.begin(_preferencesNamespace, false);
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
      _arduino->preferences.begin(_preferencesNamespace, true);
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
      _arduino->preferences.begin(_preferencesNamespace, true);
      _siteName = _arduino->preferences.getString(SITE_KEY);
      _locationName = _arduino->preferences.getString(LOCATION_KEY);
      _bucketName = _arduino->preferences.getString(BUCKET_KEY);
      _arduino->preferences.end();
   }

protected:
   SiteConfig _resolveFixedSite() override
   {
      bool reconfigure = false;

#if defined(ARDUINO_BUTTON_A_SUPPORTED)
      // buttonA is on GPIO0, a strapping pin: holding it low during power-on/reset puts
      // the chip into UART download mode instead of running the sketch, so it can't be
      // checked during boot. Instead, give the user a short window after boot to press it.
      Serial.println("Press buttonA now to reconfigure the site/location...");
      reconfigure = SiteResolver::waitForForcePrompt(_arduino->buttonA, RECONFIGURE_PROMPT_WINDOW_MS);
#else
      // This board has no button, so instead automatically offer the reprompt whenever a
      // Serial monitor is attached at boot (e.g. the board is inside an enclosure).
      if (Serial)
      {
         reconfigure = true;
      }
#endif

      if (reconfigure || !_hasSavedConfig())
      {
         _promptAndSaveSiteLocation();
      }
      else
      {
         _loadSavedConfig();
      }
      _arduino->printlnInitStatus("Location...", siteLocation().c_str());

      return SiteConfig{ nullptr, _bucketName.c_str(), _siteName.c_str(), _locationName.c_str() };
   }

public:
   ///
   /// <summary>
   /// Creates a TempMonitor bound to the given board and configuration.
   /// </summary>
   /// <param name="arduino">The board wrapper.</param>
   /// <param name="config">Shared monitor configuration.</param>
   /// <param name="preferencesNamespace">Preferences (NVS) namespace used to persist the site/location/bucket.</param>
   /// <param name="bucketOptions">Array of selectable InfluxDB bucket names.</param>
   /// <param name="numBucketOptions">Number of entries in bucketOptions.</param>
   /// <param name="resetDelayS">Seconds to wait before resetting after reportSensorFailure() is called.</param>
   ///
   TempMonitor(Arduino* arduino, const SketchConfig& config, const char* preferencesNamespace, const char* const* bucketOptions, size_t numBucketOptions, uint8_t resetDelayS)
      : Monitor(arduino, config), _preferencesNamespace(preferencesNamespace), _bucketOptions(bucketOptions), _numBucketOptions(numBucketOptions), _resetDelayS(resetDelayS)
   {
   }

   ///
   /// <summary>
   /// Formats this device's site and location as "Site/Location".
   /// </summary>
   /// <returns>The formatted "Site/Location" string.</returns>
   ///
   std::string siteLocation() const
   {
      return std::string(_siteName.c_str()) + "/" + _locationName.c_str();
   }

   ///
   /// <summary>
   /// Formats this device's bucket, site, and location as "Bucket/Site/Location".
   /// </summary>
   /// <returns>The formatted "Bucket/Site/Location" string.</returns>
   ///
   std::string bucketSiteLocation() const
   {
      return _bucketName.c_str() + std::string("/") + siteLocation();
   }

   ///
   /// <summary>
   /// Signals a fatal sensor initialization failure using the same status indicator and
   /// reset delay Monitor uses internally for its own fatal init failures.
   /// </summary>
   ///
   void reportSensorFailure()
   {
      _status->setStatus(Status::FAILED);
      Util::reset(_resetDelayS);
   }
};
