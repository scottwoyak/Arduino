#pragma once

#include <Preferences.h>
#include <span>

#include "SerialX.h"

///
/// <summary>
/// Resolves a site (chosen from a fixed list) and location (free text) pair, persisting
/// the choice in Preferences and offering to keep or re-enter it on subsequent boots.
/// Mirrors the site/location prompting flow used by MonitorSketch/PublisherSketch, for
/// sketches (e.g. viewers) that aren't InfluxDB-enabled but still want a clear,
/// non-generic site/location selection prompt instead of picking from a table of
/// pre-built topic strings.
/// </summary>
///
class SiteLocationResolver
{
private:
   static constexpr auto SITE_KEY = "site";
   static constexpr auto LOCATION_KEY = "location";
   static constexpr uint16_t PROMPT_TIMEOUT_S = 10;

   const char* _preferencesNamespace;
   std::span<const char* const> _siteOptions;
   String _siteName;
   String _locationName;

   ///
   /// <summary>
   /// Prompts the user over Serial to pick a site from _siteOptions. Blocks until a
   /// valid selection is entered.
   /// </summary>
   /// <returns>The chosen site name.</returns>
   ///
   String _promptForSite()
   {
      Serial.println("Select a site:");
      for (size_t i = 0; i < _siteOptions.size(); i++)
      {
         Serial.print("  ");
         Serial.print(i + 1);
         Serial.print(": ");
         Serial.println(_siteOptions[i]);
      }

      String label = "Enter selection (1-" + String(_siteOptions.size()) + "): ";
      long selection = SerialX::promptForInt(label, 1, (long)_siteOptions.size());
      return _siteOptions[selection - 1];
   }

   ///
   /// <summary>
   /// Prompts the user over Serial for a site and location, storing them into
   /// _siteName and _locationName. The site is chosen from _siteOptions. Blocks until
   /// both are entered/selected non-empty.
   /// </summary>
   ///
   void _promptForSiteLocation()
   {
      Serial.println("Select the air monitor location to display:");

      _siteName = _promptForSite();

      do
      {
         _locationName = SerialX::prompt("Enter location: ");
      } while (_locationName.length() == 0);
   }

   ///
   /// <summary>
   /// Asks the user, over Serial, whether to keep the currently loaded saved
   /// site/location or enter new values instead. Call only after _loadSavedConfig() has
   /// populated _siteName/_locationName. Waits up to PROMPT_TIMEOUT_S seconds for a
   /// response, defaulting to "keep saved" if none arrives.
   /// </summary>
   /// <returns>True if the saved configuration should be kept; false to reconfigure.</returns>
   ///
   bool _promptKeepSavedConfig()
   {
      String options[] = {
         "Keep saved: " + _siteName + "/" + _locationName,
         "Enter new values",
      };

      Serial.println("Reconfigure the air monitor location to display:");
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
   /// Checks whether a site/location have already been saved to Preferences.
   /// </summary>
   /// <param name="preferences">Preferences instance to read.</param>
   /// <returns>True if a saved configuration exists; otherwise false.</returns>
   ///
   bool _hasSavedConfig(Preferences& preferences)
   {
      preferences.begin(_preferencesNamespace, true);
      bool hasSavedConfig = preferences.isKey(SITE_KEY) && preferences.isKey(LOCATION_KEY);
      preferences.end();

      return hasSavedConfig;
   }

   ///
   /// <summary>
   /// Loads the saved site/location from Preferences into _siteName and _locationName.
   /// </summary>
   /// <param name="preferences">Preferences instance to read.</param>
   ///
   void _loadSavedConfig(Preferences& preferences)
   {
      preferences.begin(_preferencesNamespace, true);
      _siteName = preferences.getString(SITE_KEY);
      _locationName = preferences.getString(LOCATION_KEY);
      preferences.end();
   }

   ///
   /// <summary>
   /// Saves _siteName/_locationName to Preferences.
   /// </summary>
   /// <param name="preferences">Preferences instance to write.</param>
   ///
   void _saveConfig(Preferences& preferences)
   {
      preferences.begin(_preferencesNamespace, false);
      preferences.putString(SITE_KEY, _siteName);
      preferences.putString(LOCATION_KEY, _locationName);
      preferences.end();
   }

public:
   ///
   /// <summary>
   /// Creates a SiteLocationResolver that persists its choice under the given
   /// Preferences namespace.
   /// </summary>
   /// <param name="preferencesNamespace">Preferences (NVS) namespace used to persist the selected site/location.</param>
   /// <param name="siteOptions">Site choices offered to the user; location is entered as free text.</param>
   ///
   SiteLocationResolver(const char* preferencesNamespace, std::span<const char* const> siteOptions)
      : _preferencesNamespace(preferencesNamespace), _siteOptions(siteOptions)
   {
   }

   ///
   /// <summary>
   /// Resolves the site/location: returns the saved values unless none are saved yet or
   /// forcePrompt is true, in which case the user is prompted over Serial (offering to
   /// keep the saved values first, if any exist) and the result is saved for next time.
   /// </summary>
   /// <param name="preferences">Preferences instance to read/write (e.g. arduino.preferences).</param>
   /// <param name="forcePrompt">If true, always offers to reconfigure even if saved values exist.</param>
   ///
   void resolve(Preferences& preferences, bool forcePrompt)
   {
      bool hasSavedConfig = _hasSavedConfig(preferences);
      bool reconfigure = forcePrompt;

      if (reconfigure && hasSavedConfig)
      {
         _loadSavedConfig(preferences);
         reconfigure = !_promptKeepSavedConfig();
      }

      if (reconfigure || !hasSavedConfig)
      {
         _promptForSiteLocation();
         _saveConfig(preferences);
      }
      else
      {
         _loadSavedConfig(preferences);
      }
   }

   ///
   /// <summary>Returns the resolved site name (empty until resolve() is called).</summary>
   ///
   const String& site() const
   {
      return _siteName;
   }

   ///
   /// <summary>Returns the resolved location name (empty until resolve() is called).</summary>
   ///
   const String& location() const
   {
      return _locationName;
   }
};
