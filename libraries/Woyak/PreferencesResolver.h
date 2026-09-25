#pragma once

#include <span>
#include <vector>
#include <Preferences.h>

#include "SerialTable.h"
#include "SerialX.h"
#include "Status.h"
#include "Timer.h"

///
/// <summary>
/// Generic resolver for a table of fixed-width prompt entries (e.g. InfluxDB
/// sites/locations, telemetry topics): returns the entry saved in Preferences (NVS),
/// unless it hasn't been saved yet or a re-prompt is forced, in which case the user is
/// prompted over Serial (from a table of choices) and the selection is saved for next
/// time. Each entry is a fixed number of fields (e.g. 1 for a telemetry topic, 3 for an
/// InfluxDB bucket/site/location); entries are passed/returned as a flattened,
/// row-major array of fields (entryCount * fieldsPerEntry) so the same resolver works
/// for any entry shape, without needing a dedicated class per domain.
/// </summary>
///
class PreferencesResolver
{
private:
   /// <summary>Preferences namespace used to persist the resolved entry.</summary>
   const char* _namespace;

   /// <summary>Preferences keys, one per field (fieldsPerEntry == _keys.size()).</summary>
   std::vector<const char*> _keys;

   /// <summary>Resolved field values, owned here so returned pointers/Strings stay valid.</summary>
   std::vector<String> _values;

   ///
   /// <summary>
   /// Prints the header and a fixed-width table of entries to Serial, with a "#" index
   /// column (marking defaultIndex, if given) followed by columns for each field.
   /// </summary>
   /// <param name="header">Prompt header text.</param>
   /// <param name="columns">Column metadata for each field (fieldsPerEntry entries, not including "#").</param>
   /// <param name="entries">Flattened row-major entry table (entryCount * fieldsPerEntry fields).</param>
   /// <param name="entryCount">Number of entries in the table.</param>
   /// <param name="fieldsPerEntry">Number of fields per entry (must equal columns.size() and _keys.size()).</param>
   /// <param name="defaultIndex">Entry index to mark with "*", or SIZE_MAX for none.</param>
   ///
   static void _printTable(const char* header, std::span<const SerialTable::Column> columns, const char* const* entries, size_t entryCount, size_t fieldsPerEntry, size_t defaultIndex)
   {
      std::vector<SerialTable::Column> allColumns;
      allColumns.push_back({ "#", 5 });
      allColumns.insert(allColumns.end(), columns.begin(), columns.end());

      Serial.println(header);

      SerialTable table(nullptr, allColumns);
      table.printHeader(false);
      for (size_t i = 0; i < entryCount; i++)
      {
         std::vector<String> row;
         row.push_back(String(i + 1) + (i == defaultIndex ? "*" : ""));
         for (size_t f = 0; f < fieldsPerEntry; f++)
         {
            row.push_back(entries[i * fieldsPerEntry + f]);
         }
         table.printRow(false, row);
      }
      table.printDivider(false);
   }

   public:
   ///
   /// <summary>
   /// Creates a PreferencesResolver that persists its choice under the given
   /// Preferences namespace, using one Preferences key per field.
   /// </summary>
   /// <param name="preferencesNamespace">Preferences (NVS) namespace to read/write.</param>
   /// <param name="keys">Preferences keys, one per field (fieldsPerEntry == keys.size()).</param>
   ///
   PreferencesResolver(const char* preferencesNamespace, std::span<const char* const> keys)
      : _namespace(preferencesNamespace), _keys(keys.begin(), keys.end()), _values(keys.size())
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
   /// Prompts the user over Serial to pick an entry from the given table (marking
   /// defaultIndex as the current default) and returns its index. Falls back to
   /// defaultIndex if no valid selection is entered within PROMPT_TIMEOUT_S, so an
   /// automatically triggered prompt can't hang the device forever.
   /// </summary>
   /// <param name="header">Prompt header text, e.g. "Select a gate location:".</param>
   /// <param name="columns">Column metadata for each field.</param>
   /// <param name="entries">Flattened row-major entry table (entryCount * fieldsPerEntry fields).</param>
   /// <param name="entryCount">Number of entries in the table.</param>
   /// <param name="fieldsPerEntry">Number of fields per entry.</param>
   /// <param name="defaultIndex">Index used if the timeout elapses or the input is invalid.</param>
   /// <returns>Index into entries for the chosen (or default) entry.</returns>
   ///
   static uint8_t promptForIndex(const char* header, std::span<const SerialTable::Column> columns, const char* const* entries, size_t entryCount, size_t fieldsPerEntry, size_t defaultIndex)
   {
      _printTable(header, columns, entries, entryCount, fieldsPerEntry, defaultIndex);

      size_t index = SerialX::readSelectionWithTimeout(entryCount, defaultIndex, PROMPT_TIMEOUT_S * 1000UL);

      return (uint8_t)index;
   }

   ///
   /// <summary>
   /// Prompts the user over Serial to pick an entry from the given table, with no
   /// default and no timeout: used when there is no saved entry yet, so the device
   /// can't proceed with a meaningless default. If a Serial monitor is attached, this
   /// blocks (reprompting on invalid input) until a valid selection is entered. If no
   /// Serial monitor is attached, there's no way to prompt, so status is set to
   /// Status::FAILED (solid red) and this blocks forever.
   /// </summary>
   /// <param name="status">Status indicator to set to Status::FAILED if no Serial monitor is attached.</param>
   /// <param name="header">Prompt header text, e.g. "Select a gate location:".</param>
   /// <param name="columns">Column metadata for each field.</param>
   /// <param name="entries">Flattened row-major entry table (entryCount * fieldsPerEntry fields).</param>
   /// <param name="entryCount">Number of entries in the table.</param>
   /// <param name="fieldsPerEntry">Number of fields per entry.</param>
   /// <returns>Index into entries for the chosen entry. Never returns if no Serial monitor is attached.</returns>
   ///
   static uint8_t promptForRequiredIndex(IStatus* status, const char* header, std::span<const SerialTable::Column> columns, const char* const* entries, size_t entryCount, size_t fieldsPerEntry)
   {
      if (!Serial)
      {
         status->setStatus(Status::FAILED);
         while (true)
         {
            delay(1000);
         }
      }

      _printTable(header, columns, entries, entryCount, fieldsPerEntry, SIZE_MAX);

      String label = "Enter selection (1-" + String(entryCount) + "): ";
      size_t index = (size_t)(SerialX::promptForInt(label, 1, (long)entryCount) - 1);

      return (uint8_t)index;
   }

   ///
   /// <summary>
   /// Resolves which entry to use: returns the entry saved in Preferences, unless not
   /// all fields have been saved yet or forcePrompt is true, in which case the user is
   /// prompted over Serial (from entries) and the choice is saved for next time. If
   /// there is no saved entry yet, the user must make a selection (see
   /// promptForRequiredIndex) rather than falling back to a default after a timeout.
   /// </summary>
   /// <param name="preferences">Preferences instance to read/write (e.g. arduino.preferences).</param>
   /// <param name="status">Status indicator, set to Status::FAILED if there's no saved entry and no Serial monitor is attached.</param>
   /// <param name="promptHeader">Prompt header text used if the user must be asked.</param>
   /// <param name="columns">Column metadata for each field, shown in the prompt table.</param>
   /// <param name="entries">Flattened row-major entry table (entryCount * fieldsPerEntry fields).</param>
   /// <param name="entryCount">Number of entries in the table.</param>
   /// <param name="forcePrompt">If true, always prompts even if a saved entry exists.</param>
   /// <returns>The resolved field values, backed by this resolver's storage.</returns>
   ///
   std::span<const String> resolve(Preferences& preferences, IStatus* status, const char* promptHeader, std::span<const SerialTable::Column> columns, const char* const* entries, size_t entryCount, bool forcePrompt)
   {
      size_t fieldsPerEntry = _keys.size();

      preferences.begin(_namespace, true);
      bool hasSavedEntry = true;
      for (const char* key : _keys)
      {
         hasSavedEntry &= preferences.isKey(key);
      }
      if (hasSavedEntry)
      {
         for (size_t f = 0; f < fieldsPerEntry; f++)
         {
            _values[f] = preferences.getString(_keys[f]);
         }
      }
      preferences.end();

      size_t matchedIndex = 0;
      if (hasSavedEntry)
      {
         for (size_t i = 0; i < entryCount; i++)
         {
            bool matches = true;
            for (size_t f = 0; f < fieldsPerEntry; f++)
            {
               if (_values[f] != entries[i * fieldsPerEntry + f])
               {
                  matches = false;
                  break;
               }
            }
            if (matches)
            {
               matchedIndex = i;
               break;
            }
         }
      }

      if (!hasSavedEntry || forcePrompt)
      {
         uint8_t selectedIndex;
         if (hasSavedEntry)
         {
            selectedIndex = promptForIndex(promptHeader, columns, entries, entryCount, fieldsPerEntry, matchedIndex);
         }
         else
         {
            selectedIndex = promptForRequiredIndex(status, promptHeader, columns, entries, entryCount, fieldsPerEntry);
         }

         for (size_t f = 0; f < fieldsPerEntry; f++)
         {
            _values[f] = entries[selectedIndex * fieldsPerEntry + f];
         }

         preferences.begin(_namespace, false);
         for (size_t f = 0; f < fieldsPerEntry; f++)
         {
            preferences.putString(_keys[f], _values[f]);
         }
         preferences.end();
      }

      return _values;
   }
};
