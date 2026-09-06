#pragma once

#include <iterator>
#include <Preferences.h>

#include "SerialTable.h"
#include "SerialX.h"
#include "Timer.h"

///
/// <summary>
/// One selectable telemetry topic / InfluxDB location entry, e.g. one gate, wind, or
/// wave site. Shared by all Publisher sketches (see Publisher.h) so the site table
/// format and Preferences persistence logic stay identical across them.
/// </summary>
///
struct SiteConfig
{
   const char* telemetryTopic;
   const char* influxBucket;
   const char* influxSite;
   const char* influxLocation;
};

///
/// <summary>
/// A pointer/count pair for a SiteConfig table, constructed automatically from a
/// SiteConfig array so callers don't need to pass the count separately (e.g.
/// PublisherConfig::sites = GATE_LOCATIONS).
/// </summary>
///
struct SiteTable
{
   const SiteConfig* sites;
   size_t count;

   /// <summary>Empty table; used when a Publisher sketch has a single fixed telemetry topic (see PublisherConfig::telemetryTopic) instead of a selectable site table.</summary>
   SiteTable() : sites(nullptr), count(0)
   {
   }

   template <size_t N>
   SiteTable(const SiteConfig (&array)[N]) : sites(array), count(N)
   {
   }
};


///
/// <summary>
/// Resolves which SiteConfig entry a Publisher sketch should use: either the one saved
/// in Preferences (NVS), or one chosen by the user over Serial (which is then saved for
/// next time). Owns the resolved values so callers get stable String/const char*
/// storage instead of relying on function-local statics.
/// </summary>
///
class SiteResolver
{
private:
   /// <summary>Preferences namespace and keys used to persist the resolved site.</summary>
   const char* _namespace;
   static constexpr auto TOPIC_KEY = "topic";
   static constexpr auto BUCKET_KEY = "bucket";
   static constexpr auto SITE_KEY = "site";
   static constexpr auto LOCATION_KEY = "location";

   /// <summary>Resolved values, owned here so returned pointers stay valid.</summary>
   String _topic;
   String _bucket;
   String _site;
   String _location;

   public:
   ///
   /// <summary>
   /// Formats a SiteConfig entry with a labeled key="value" pair for each field, e.g.
   /// Bucket="Monitor" Measurement="Sensors" Sensor="Gate" Site="Bragg" Location="Left"
   /// Telemetry Topic="Gate/Left".
   /// </summary>
   /// <param name="site">The entry to format.</param>
   /// <param name="measurement">Influx measurement name shared by all entries in the site table.</param>
   /// <param name="sensor">Influx "sensor" tag value shared by all entries in the site table.</param>
   /// <returns>The formatted description.</returns>
   ///
   static String describe(const SiteConfig& site, const char* measurement, const char* sensor)
   {
      return String("Bucket=\"") + site.influxBucket + "\" Measurement=\"" + measurement + "\" Sensor=\"" + sensor + "\" Site=\"" + site.influxSite + "\" Location=\"" + site.influxLocation + "\" Telemetry Topic=\"" + site.telemetryTopic + "\"";
   }

   ///
   /// <summary>
   /// Creates a SiteResolver that persists its choice under the given Preferences namespace.
   /// </summary>
   /// <param name="preferencesNamespace">Preferences (NVS) namespace to read/write.</param>
   ///
   explicit SiteResolver(const char* preferencesNamespace) : _namespace(preferencesNamespace)
   {
   }

   ///
   /// <summary>
   /// Blocks for up to windowMs after boot, giving the user a chance to press buttonA to
   /// force a re-prompt. Intended to run before other begin() calls, since buttonA (GPIO0,
   /// a strapping pin) can't be checked at power-on/reset.
   /// </summary>
   /// <param name="button">The button to poll (typically arduino.buttonA).</param>
   /// <param name="windowMs">How long to wait for a press, in milliseconds.</param>
   /// <returns>True if buttonA was pressed within the window.</returns>
   ///
   template <typename TButton>
   static bool waitForForcePrompt(TButton& button, uint16_t windowMs)
   {
      Timer forcePromptTimer(windowMs);
      while (!forcePromptTimer.ready())
      {
         if (button.isPressed())
         {
            return true;
         }
      }
      return false;
   }

   /// <summary>How long to wait for a selection before falling back to the current default, in seconds.</summary>
   static constexpr uint16_t PROMPT_TIMEOUT_S = 10;

   ///
   /// <summary>
   /// Prompts the user over Serial to pick a site from the given table (marking
   /// defaultIndex as the current default) and returns its index. Falls back to
   /// defaultIndex if no valid selection is entered within PROMPT_TIMEOUT_S, so an
   /// automatically triggered prompt can't hang the device forever.
   /// </summary>
   /// <param name="header">Prompt header text, e.g. "Select a gate location:".</param>
   /// <param name="measurement">Influx measurement name shared by all entries in sites, shown in its own table column.</param>
   /// <param name="sensor">Influx "sensor" tag value shared by all entries in sites, shown in its own table column.</param>
   /// <param name="sites">Site table to choose from.</param>
   /// <param name="count">Number of entries in sites.</param>
   /// <param name="defaultIndex">Index used if the timeout elapses or the input is invalid.</param>
   /// <returns>Index into sites for the chosen (or default) entry.</returns>
   ///
   static uint8_t promptForIndex(const char* header, const char* measurement, const char* sensor, const SiteConfig sites[], size_t count, size_t defaultIndex)
   {
      static constexpr SerialTable::Column COLUMNS[] = {
         { "#", 5 },
         { "Bucket", 13 },
         { "Measurement", 15 },
         { "Sensor", 11 },
         { "Site", 11 },
         { "Location", 13 },
         { "Telemetry Topic", 19 },
      };

      Serial.println(header);

      SerialTable table(nullptr, COLUMNS);
      table.printHeader();
      for (size_t i = 0; i < count; i++)
      {
         String number = String(i + 1) + (i == defaultIndex ? "*" : "");
         table.printRow(number, sites[i].influxBucket, measurement, sensor, sites[i].influxSite, sites[i].influxLocation, sites[i].telemetryTopic);
      }
      table.printDivider();

      size_t index = SerialX::readSelectionWithTimeout(count, defaultIndex, PROMPT_TIMEOUT_S * 1000UL);

      Serial.print("Site: ");
      Serial.println(describe(sites[index], measurement, sensor));

      return (uint8_t)index;
   }

   ///
   /// <summary>
   /// Resolves which site to use: returns the entry saved in Preferences, unless not all
   /// 4 values have been saved yet or forcePrompt is true, in which case the user is
   /// prompted over Serial (from sites) and the choice is saved for next time.
   /// </summary>
   /// <param name="preferences">Preferences instance to read/write (e.g. arduino.preferences).</param>
   /// <param name="promptHeader">Prompt header text used if the user must be asked.</param>
   /// <param name="measurement">Influx measurement name shared by all entries in sites, shown in the prompt table.</param>
   /// <param name="sensor">Influx "sensor" tag value shared by all entries in sites, shown in the prompt table.</param>
   /// <param name="sites">Site table to choose from.</param>
   /// <param name="count">Number of entries in sites.</param>
   /// <param name="forcePrompt">If true, always prompts even if a saved site exists.</param>
   /// <returns>The resolved SiteConfig, backed by this resolver's storage.</returns>
   ///
   SiteConfig resolve(Preferences& preferences, const char* promptHeader, const char* measurement, const char* sensor, const SiteConfig sites[], size_t count, bool forcePrompt)
   {
      preferences.begin(_namespace, true);
      bool hasSavedSite = preferences.isKey(TOPIC_KEY) && preferences.isKey(BUCKET_KEY) && preferences.isKey(SITE_KEY) && preferences.isKey(LOCATION_KEY);
      if (hasSavedSite)
      {
         _topic = preferences.getString(TOPIC_KEY);
         _bucket = preferences.getString(BUCKET_KEY);
         _site = preferences.getString(SITE_KEY);
         _location = preferences.getString(LOCATION_KEY);
      }
      preferences.end();

      if (!hasSavedSite || forcePrompt)
      {
         size_t defaultIndex = 0;
         if (hasSavedSite)
         {
            for (size_t i = 0; i < count; i++)
            {
               if (_topic == sites[i].telemetryTopic)
               {
                  defaultIndex = i;
                  break;
               }
            }
         }

         const SiteConfig& selected = sites[promptForIndex(promptHeader, measurement, sensor, sites, count, defaultIndex)];
         _topic = selected.telemetryTopic;
         _bucket = selected.influxBucket;
         _site = selected.influxSite;
         _location = selected.influxLocation;

         preferences.begin(_namespace, false);
         preferences.putString(TOPIC_KEY, _topic);
         preferences.putString(BUCKET_KEY, _bucket);
         preferences.putString(SITE_KEY, _site);
         preferences.putString(LOCATION_KEY, _location);
         preferences.end();
      }

      return { _topic.c_str(), _bucket.c_str(), _site.c_str(), _location.c_str() };
   }
};
