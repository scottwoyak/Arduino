#pragma once

#include <span>
#include <Preferences.h>

#include "SerialTable.h"
#include "SerialX.h"
#include "Status.h"
#include "Timer.h"

///
/// <summary>
/// Fixed InfluxDB site/location entry, e.g. one gate, wind, or wave site. Used by
/// SketchConfig::influx (both as the single fixed entry via context, and within the
/// selectable prompts table) so the site table format and Preferences persistence
/// logic stay identical across Monitor and Publisher sketches.
/// </summary>
///
struct InfluxContext
{
   /// <summary>InfluxDB bucket this site's points are written to.</summary>
   const char* bucket = nullptr;

   /// <summary>Value for the "site" tag attached to points logged for this site.</summary>
   const char* site = nullptr;

   /// <summary>Value for the "location" tag attached to points logged for this site.</summary>
   const char* location = nullptr;

   /// <summary>Value for the "sensor" tag attached to points logged for this site.</summary>
   const char* sensor = nullptr;
};

///
/// <summary>
/// All sketch-wide InfluxDB settings for a Monitor/Publisher sketch: measurement
/// names, post cadence, rolling-average sample count, and site selection (either a
/// single fixed site, or a table of selectable sites). Grouped together here (rather
/// than left flat on SketchConfig) since these values apply uniformly across every
/// entry in prompts - unlike bucket/site/location/sensor, which vary per entry (see
/// InfluxContext).
/// </summary>
///
struct InfluxConfig
{
   /// <summary>Fixed InfluxDB site+location(+sensor) entry used when prompts is empty (no selection prompt). Ignored if prompts is non-empty. If promptForContext is set (Monitor only), only its sensor field is used (bucket/site/location are prompted for instead).</summary>
   InfluxContext context = { INFLUXDB_BUCKET, nullptr, nullptr };

   /// <summary>Table of selectable InfluxDB site+location entries. Leave empty for a sketch with a single fixed site/location (see context) instead of a user-selectable table.</summary>
   std::span<const InfluxContext> prompts;

   /// <summary>Influx measurement name used for the standard enclosure/CPU points and any points added via addPoint().</summary>
   const char* measurement = "Sensors";

   /// <summary>Influx measurement name used for the single startup/OTA log point.</summary>
   const char* logMeasurement = "Log";

   /// <summary>How often (in seconds) queued Influx points are posted/flushed.</summary>
   uint16_t intervalS = 60;

   /// <summary>Number of samples averaged for the standard rolling-average enclosure temperature/humidity fields.</summary>
   size_t rollingSamples = 10;

   /// <summary>Monitor only: if true, prompts over Serial (or loads the saved values from Preferences) for a bucket (chosen from Monitor's shared BUCKET_OPTIONS list), a site (chosen from Monitor's shared SITE_OPTIONS list), and a free-text location, instead of using context.</summary>
   bool promptForContext = false;

   /// <summary>Monitor only: seconds to wait before resetting after reportSensorFailure() is called.</summary>
   uint8_t sensorFailureResetDelayS = 10;
};

///
/// <summary>
/// Publisher's telemetry settings: either a table of selectable telemetry topics, or a
/// single fixed topic, plus the decimal precision and publish cadence used when
/// streaming the value over the WebSocket connection. Grouped together here (rather
/// than left flat on SketchConfig) since these values are only used by Publisher
/// sketches.
/// </summary>
///
struct TelemetryConfig
{
   /// <summary>Table of selectable telemetry topics, prompted for independently of influx.prompts. Leave empty for a sketch with a single fixed topic (see topic) instead of a user-selectable table.</summary>
   std::span<const char* const> prompts;

   /// <summary>Fixed telemetry topic used when prompts is empty (no selection prompt). Ignored if prompts is non-empty.</summary>
   const char* topic = nullptr;

   /// <summary>Decimal places used when publishing the telemetry value over the WebSocket connection.</summary>
   uint8_t decimals = 2;

   /// <summary>How often (in milliseconds) the telemetry value source is read and published. 0 means every loop() iteration.</summary>
   uint16_t publishIntervalMs = 0;
};

///
/// <summary>
/// Resolves which InfluxDB site to use: either the one saved in Preferences (NVS), or
/// one chosen by the user over Serial (which is then saved for next time). Owns the
/// resolved values so callers get stable String/const char* storage instead of relying
/// on function-local statics.
/// </summary>
///
class InfluxContextResolver
{
private:
   /// <summary>Preferences namespace and keys used to persist the resolved site.</summary>
   const char* _namespace;
   static constexpr auto BUCKET_KEY = "bucket";
   static constexpr auto SITE_KEY = "site";
   static constexpr auto LOCATION_KEY = "location";

   /// <summary>Resolved values, owned here so returned pointers stay valid.</summary>
   String _bucket;
   String _site;
   String _location;

   public:
   ///
   /// <summary>
   /// Formats an InfluxContext entry with a labeled key="value" pair for each field,
   /// e.g. Bucket="Monitor" Measurement="Sensors" Sensor="Gate" Site="Bragg"
   /// Location="Left".
   /// </summary>
   /// <param name="site">The entry to format.</param>
   /// <param name="measurement">Influx measurement name shared by all entries in the site table.</param>
   /// <returns>The formatted description.</returns>
   ///
   static String describe(const InfluxContext& site, const char* measurement)
   {
      return String("Bucket=\"") + site.bucket + "\" Measurement=\"" + measurement + "\" Sensor=\"" + (site.sensor != nullptr ? site.sensor : "") + "\" Site=\"" + site.site + "\" Location=\"" + site.location + "\"";
   }

   ///
   /// <summary>
   /// Creates an InfluxContextResolver that persists its choice under the given Preferences namespace.
   /// </summary>
   /// <param name="preferencesNamespace">Preferences (NVS) namespace to read/write.</param>
   ///
   explicit InfluxContextResolver(const char* preferencesNamespace) : _namespace(preferencesNamespace)
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
   /// <param name="sites">Site table to choose from.</param>
   /// <param name="count">Number of entries in sites.</param>
   /// <param name="defaultIndex">Index used if the timeout elapses or the input is invalid.</param>
   /// <returns>Index into sites for the chosen (or default) entry.</returns>
   ///
   static uint8_t promptForIndex(const char* header, const char* measurement, const InfluxContext sites[], size_t count, size_t defaultIndex)
   {
      static constexpr SerialTable::Column COLUMNS[] = {
         { "#", 5 },
         { "Bucket", 13 },
         { "Measurement", 15 },
         { "Sensor", 11 },
         { "Site", 11 },
         { "Location", 13 },
      };

      Serial.println(header);

      SerialTable table(nullptr, COLUMNS);
      table.printHeader();
      for (size_t i = 0; i < count; i++)
      {
         String number = String(i + 1) + (i == defaultIndex ? "*" : "");
         table.printRow(number, sites[i].bucket, measurement, sites[i].sensor, sites[i].site, sites[i].location);
      }
      table.printDivider();

      size_t index = SerialX::readSelectionWithTimeout(count, defaultIndex, PROMPT_TIMEOUT_S * 1000UL);

      Serial.print("Site: ");
      Serial.println(describe(sites[index], measurement));

      return (uint8_t)index;
   }

   ///
   /// <summary>
   /// Prompts the user over Serial to pick a site from the given table, with no
   /// default and no timeout: used when there is no saved site yet, so the device
   /// can't proceed with a meaningless default. If a Serial monitor is attached, this
   /// blocks (reprompting on invalid input) until a valid selection is entered. If no
   /// Serial monitor is attached, there's no way to prompt, so status is set to
   /// Status::FAILED (solid red) and this blocks forever.
   /// </summary>
   /// <param name="status">Status indicator to set to Status::FAILED if no Serial monitor is attached.</param>
   /// <param name="header">Prompt header text, e.g. "Select a gate location:".</param>
   /// <param name="measurement">Influx measurement name shared by all entries in sites, shown in its own table column.</param>
   /// <param name="sites">Site table to choose from.</param>
   /// <param name="count">Number of entries in sites.</param>
   /// <returns>Index into sites for the chosen entry. Never returns if no Serial monitor is attached.</returns>
   ///
   static uint8_t promptForRequiredIndex(IStatus* status, const char* header, const char* measurement, const InfluxContext sites[], size_t count)
   {
      if (!Serial)
      {
         status->setStatus(Status::FAILED);
         while (true)
         {
            delay(1000);
         }
      }

      static constexpr SerialTable::Column COLUMNS[] = {
         { "#", 5 },
         { "Bucket", 13 },
         { "Measurement", 15 },
         { "Sensor", 11 },
         { "Site", 11 },
         { "Location", 13 },
      };

      Serial.println(header);

      SerialTable table(nullptr, COLUMNS);
      table.printHeader();
      for (size_t i = 0; i < count; i++)
      {
         table.printRow(String(i + 1), sites[i].bucket, measurement, sites[i].sensor, sites[i].site, sites[i].location);
      }
      table.printDivider();

      String label = "Enter selection (1-" + String(count) + "): ";
      size_t index = (size_t)(SerialX::promptForInt(label, 1, (long)count) - 1);

      Serial.print("Site: ");
      Serial.println(describe(sites[index], measurement));

      return (uint8_t)index;
   }

   ///
   /// <summary>
   /// Resolves which site to use: returns the entry saved in Preferences, unless not all
   /// 3 values have been saved yet or forcePrompt is true, in which case the user is
   /// prompted over Serial (from sites) and the choice is saved for next time. If there
   /// is no saved site yet, the user must make a selection (see promptForRequiredIndex)
   /// rather than falling back to a default after a timeout.
   /// </summary>
   /// <param name="preferences">Preferences instance to read/write (e.g. arduino.preferences).</param>
   /// <param name="status">Status indicator, set to Status::FAILED if there's no saved site and no Serial monitor is attached.</param>
   /// <param name="promptHeader">Prompt header text used if the user must be asked.</param>
   /// <param name="measurement">Influx measurement name shared by all entries in sites, shown in the prompt table.</param>
   /// <param name="sites">Site table to choose from.</param>
   /// <param name="count">Number of entries in sites.</param>
   /// <param name="forcePrompt">If true, always prompts even if a saved site exists.</param>
   /// <returns>The resolved InfluxContext, backed by this resolver's storage.</returns>
   ///
   InfluxContext resolve(Preferences& preferences, IStatus* status, const char* promptHeader, const char* measurement, const InfluxContext sites[], size_t count, bool forcePrompt)
   {
      preferences.begin(_namespace, true);
      bool hasSavedSite = preferences.isKey(BUCKET_KEY) && preferences.isKey(SITE_KEY) && preferences.isKey(LOCATION_KEY);
      if (hasSavedSite)
      {
         _bucket = preferences.getString(BUCKET_KEY);
         _site = preferences.getString(SITE_KEY);
         _location = preferences.getString(LOCATION_KEY);
      }
      preferences.end();

      size_t matchedIndex = 0;
      bool hasMatch = false;
      if (hasSavedSite)
      {
         for (size_t i = 0; i < count; i++)
         {
            if (_site == sites[i].site && _location == sites[i].location)
            {
               matchedIndex = i;
               hasMatch = true;
               break;
            }
         }
      }

      if (!hasSavedSite || forcePrompt)
      {
         uint8_t selectedIndex;
         if (hasSavedSite)
         {
            selectedIndex = promptForIndex(promptHeader, measurement, sites, count, matchedIndex);
         }
         else
         {
            selectedIndex = promptForRequiredIndex(status, promptHeader, measurement, sites, count);
         }

         const InfluxContext& selected = sites[selectedIndex];
         _bucket = selected.bucket;
         _site = selected.site;
         _location = selected.location;
         matchedIndex = selectedIndex;
         hasMatch = true;

         preferences.begin(_namespace, false);
         preferences.putString(BUCKET_KEY, _bucket);
         preferences.putString(SITE_KEY, _site);
         preferences.putString(LOCATION_KEY, _location);
         preferences.end();
      }

      const char* sensor = hasMatch ? sites[matchedIndex].sensor : nullptr;
      return { _bucket.c_str(), _site.c_str(), _location.c_str(), sensor };
   }
};

///
/// <summary>
/// Resolves which telemetry topic a Publisher sketch should use: either the one saved
/// in Preferences (NVS), or one chosen by the user over Serial (which is then saved for
/// next time). Owns the resolved value so callers get stable String/const char* storage
/// instead of relying on function-local statics.
/// </summary>
///
class TelemetryTopicResolver
{
private:
   /// <summary>Preferences namespace and key used to persist the resolved topic.</summary>
   const char* _namespace;
   static constexpr auto TOPIC_KEY = "topic";

   /// <summary>Resolved value, owned here so returned pointers stay valid.</summary>
   String _topic;

   public:
   ///
   /// <summary>
   /// Creates a TelemetryTopicResolver that persists its choice under the given Preferences namespace.
   /// </summary>
   /// <param name="preferencesNamespace">Preferences (NVS) namespace to read/write.</param>
   ///
   explicit TelemetryTopicResolver(const char* preferencesNamespace) : _namespace(preferencesNamespace)
   {
   }

   /// <summary>How long to wait for a selection before falling back to the current default, in seconds.</summary>
   static constexpr uint16_t PROMPT_TIMEOUT_S = 10;

   ///
   /// <summary>
   /// Prompts the user over Serial to pick a telemetry topic from the given table
   /// (marking defaultIndex as the current default) and returns its index. Falls back
   /// to defaultIndex if no valid selection is entered within PROMPT_TIMEOUT_S, so an
   /// automatically triggered prompt can't hang the device forever.
   /// </summary>
   /// <param name="header">Prompt header text, e.g. "Select a telemetry topic:".</param>
   /// <param name="topics">Telemetry topic table to choose from.</param>
   /// <param name="count">Number of entries in topics.</param>
   /// <param name="defaultIndex">Index used if the timeout elapses or the input is invalid.</param>
   /// <returns>Index into topics for the chosen (or default) entry.</returns>
   ///
   static uint8_t promptForIndex(const char* header, const char* const topics[], size_t count, size_t defaultIndex)
   {
      static constexpr SerialTable::Column COLUMNS[] = {
         { "#", 5 },
         { "Topic", 24 },
      };

      Serial.println(header);

      SerialTable table(nullptr, COLUMNS);
      table.printHeader();
      for (size_t i = 0; i < count; i++)
      {
         String number = String(i + 1) + (i == defaultIndex ? "*" : "");
         table.printRow(number, topics[i]);
      }
      table.printDivider();

      size_t index = SerialX::readSelectionWithTimeout(count, defaultIndex, PROMPT_TIMEOUT_S * 1000UL);

      return (uint8_t)index;
   }

   ///
   /// <summary>
   /// Prompts the user over Serial to pick a telemetry topic from the given table, with
   /// no default and no timeout: used when there is no saved topic yet, so the device
   /// can't proceed with a meaningless default. If a Serial monitor is attached, this
   /// blocks (reprompting on invalid input) until a valid selection is entered. If no
   /// Serial monitor is attached, there's no way to prompt, so status is set to
   /// Status::FAILED (solid red) and this blocks forever.
   /// </summary>
   /// <param name="status">Status indicator to set to Status::FAILED if no Serial monitor is attached.</param>
   /// <param name="header">Prompt header text, e.g. "Select a telemetry topic:".</param>
   /// <param name="topics">Telemetry topic table to choose from.</param>
   /// <param name="count">Number of entries in topics.</param>
   /// <returns>Index into topics for the chosen entry. Never returns if no Serial monitor is attached.</returns>
   ///
   static uint8_t promptForRequiredIndex(IStatus* status, const char* header, const char* const topics[], size_t count)
   {
      if (!Serial)
      {
         status->setStatus(Status::FAILED);
         while (true)
         {
            delay(1000);
         }
      }

      static constexpr SerialTable::Column COLUMNS[] = {
         { "#", 5 },
         { "Topic", 24 },
      };

      Serial.println(header);

      SerialTable table(nullptr, COLUMNS);
      table.printHeader();
      for (size_t i = 0; i < count; i++)
      {
         table.printRow(String(i + 1), topics[i]);
      }
      table.printDivider();

      String label = "Enter selection (1-" + String(count) + "): ";
      size_t index = (size_t)(SerialX::promptForInt(label, 1, (long)count) - 1);

      return (uint8_t)index;
   }

   ///
   /// <summary>
   /// Resolves which telemetry topic to use: returns the value saved in Preferences,
   /// unless it hasn't been saved yet or forcePrompt is true, in which case the user is
   /// prompted over Serial (from topics) and the choice is saved for next time. If
   /// there is no saved topic yet, the user must make a selection (see
   /// promptForRequiredIndex) rather than falling back to a default after a timeout.
   /// </summary>
   /// <param name="preferences">Preferences instance to read/write (e.g. arduino.preferences).</param>
   /// <param name="status">Status indicator, set to Status::FAILED if there's no saved topic and no Serial monitor is attached.</param>
   /// <param name="promptHeader">Prompt header text used if the user must be asked.</param>
   /// <param name="topics">Telemetry topic table to choose from.</param>
   /// <param name="count">Number of entries in topics.</param>
   /// <param name="forcePrompt">If true, always prompts even if a saved topic exists.</param>
   /// <returns>The resolved topic, backed by this resolver's storage.</returns>
   ///
   const char* resolve(Preferences& preferences, IStatus* status, const char* promptHeader, const char* const topics[], size_t count, bool forcePrompt)
   {
      preferences.begin(_namespace, true);
      bool hasSavedTopic = preferences.isKey(TOPIC_KEY);
      if (hasSavedTopic)
      {
         _topic = preferences.getString(TOPIC_KEY);
      }
      preferences.end();

      if (!hasSavedTopic || forcePrompt)
      {
         uint8_t selectedIndex;
         if (hasSavedTopic)
         {
            size_t defaultIndex = 0;
            for (size_t i = 0; i < count; i++)
            {
               if (_topic == topics[i])
               {
                  defaultIndex = i;
                  break;
               }
            }

            selectedIndex = promptForIndex(promptHeader, topics, count, defaultIndex);
         }
         else
         {
            selectedIndex = promptForRequiredIndex(status, promptHeader, topics, count);
         }

         _topic = topics[selectedIndex];

         preferences.begin(_namespace, false);
         preferences.putString(TOPIC_KEY, _topic);
         preferences.end();
      }

      return _topic.c_str();
   }
};
