#pragma once

#include <string>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "Util.h"
#include "Logger.h"

// Display-based methods are only available on boards with a display.
// ARDUINO_DISPLAY_SUPPORTED is defined by ArduinoBoard.h when the target board has one.
#ifdef ARDUINO_DISPLAY_SUPPORTED
#include "ArduinoWithDisplay.h"
#endif

///
/// <summary>
/// Looks up a device's configuration fields from a shared JSON file, keyed by the
/// device's WiFi MAC address. The JSON file is expected to be an object mapping MAC
/// addresses (as returned by WiFi.macAddress(), e.g. "AA:BB:CC:DD:EE:FF") to an object of
/// arbitrary fields, e.g.:
/// <code>
/// {
///    "AA:BB:CC:DD:EE:FF": { "location": "Cabin", "site": "Lake" },
///    "11:22:33:44:55:66": { "location": "Printer", "site": "Bragg", "threshold": 42 }
/// }
/// </code>
/// Any fields can be present in each device's entry; use get() to retrieve a field by
/// name.
/// </summary>
///
class DeviceConfig
{
private:
   static constexpr uint8_t _HEADING_TEXT_SIZE = 2;
   static constexpr uint8_t _MAC_TEXT_SIZE = 3;

   JsonDocument _doc;
   JsonObject _entry;

   ///
   /// <summary>
   /// Result of a single fetch-and-lookup attempt.
   /// </summary>
   ///
   enum class _Result
   {
      OK,
      FETCH_FAILED,
      MAC_NOT_FOUND
   };

   ///
   /// <summary>
   /// Extracts the file name (the portion after the last '/') from a URL, for display in
   /// fix instructions.
   /// </summary>
   /// <param name="url">The URL to extract the file name from.</param>
   /// <returns>The file name portion of the URL.</returns>
   ///
   String _fileNameFromUrl(const char* url)
   {
      String urlString(url);
      int lastSlash = urlString.lastIndexOf('/');
      return (lastSlash < 0) ? urlString : urlString.substring(lastSlash + 1);
   }

   ///
   /// <summary>
   /// Checks this device's config entry for a set of required fields.
   /// </summary>
   /// <param name="requiredKeys">Field names that must be present in the entry.</param>
   /// <returns>The name of the first missing field, or nullptr if all are present.</returns>
   ///
   const char* _findMissingKey(std::initializer_list<const char*> requiredKeys) const
   {
      for (const char* key : requiredKeys)
      {
         if (_entry[key].isNull())
         {
            return key;
         }
      }
      return nullptr;
   }

   ///
   /// <summary>
   /// Performs a single fetch-and-lookup attempt.
   /// </summary>
   /// <param name="configUrl">URL of the shared JSON config file.</param>
   /// <param name="mac">This device's MAC address, used as the lookup key.</param>
   /// <returns>The result of the attempt.</returns>
   ///
   _Result _fetchAndLookup(const char* configUrl, const String& mac)
   {
      HTTPClient http;
      http.begin(configUrl);
      int httpCode = http.GET();

      if (httpCode != HTTP_CODE_OK)
      {
         Logger.log("DeviceConfig: HTTP GET failed, code: " + String(httpCode), LogSeverity::ERROR);
         http.end();
         return _Result::FETCH_FAILED;
      }

      String payload = http.getString();
      http.end();

      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (error)
      {
         Logger.log("DeviceConfig: JSON parse failed: " + String(error.c_str()), LogSeverity::ERROR);
         Logger.log("DeviceConfig: Response payload was: " + payload, LogSeverity::ERROR);
         return _Result::FETCH_FAILED;
      }

      JsonObject deviceEntry = doc[mac];
      if (deviceEntry.isNull())
      {
         Serial.print("DeviceConfig: MAC address not found in config: ");
         Serial.println(mac);
         Serial.println("DeviceConfig: Response payload was:");
         Serial.println(payload);
         return _Result::MAC_NOT_FOUND;
      }

      _doc = doc;
      _entry = _doc[mac];

      Serial.println("DeviceConfig: Loaded entry:");
      for (JsonPair field : _entry)
      {
         Serial.print("   ");
         Serial.print(field.key().c_str());
         Serial.print(": ");
         Serial.println(field.value().as<String>());
      }

      return _Result::OK;
   }

public:
   ///
   /// <summary>
   /// Gets a field's value from this device's config entry as a string.
   /// </summary>
   /// <param name="key">Name of the field to retrieve (e.g. "location").</param>
   /// <returns>The field's string value, or an empty string if the field isn't present.</returns>
   ///
   const char* get(const char* key) const
   {
      return _entry[key] | "";
   }

   ///
   /// <summary>
   /// Gets a field's value from this device's config entry as a float.
   /// </summary>
   /// <param name="key">Name of the field to retrieve.</param>
   /// <returns>The field's float value, or 0 if the field isn't present.</returns>
   ///
   float getFloat(const char* key) const
   {
      return _entry[key] | 0.0f;
   }

   ///
   /// <summary>
   /// Fetches the shared config file over HTTP(S) and looks up this device's location/
   /// site by its WiFi MAC address. WiFi must already be connected before calling this.
   /// Retries indefinitely (with a delay between attempts) until the fetch and lookup
   /// both succeed, since callers treat this as a hard startup requirement. If a display
   /// is provided, shows fix instructions and the device's MAC address on-screen whenever
   /// the MAC address isn't found in the config (or a required field is missing from its
   /// entry), so a bad/missing entry is easy to correct.
   /// </summary>
   /// <param name="configUrl">URL of the shared JSON config file.</param>
   /// <param name="requiredKeys">Field names that must be present in this device's entry (e.g. {"site", "location"}).</param>
   /// <param name="arduino">Optional display to show fix instructions on if the MAC isn't found or a required field is missing.</param>
   /// <param name="retryDelayMs">Milliseconds to wait between retry attempts.</param>
   ///
#ifdef ARDUINO_DISPLAY_SUPPORTED
   void begin(const char* configUrl, std::initializer_list<const char*> requiredKeys = {}, ArduinoWithDisplay* arduino = nullptr, uint32_t retryDelayMs = 5000)
#else
   void begin(const char* configUrl, std::initializer_list<const char*> requiredKeys = {}, uint32_t retryDelayMs = 5000)
#endif
   {
      String mac = WiFi.macAddress();
      String configFileName = _fileNameFromUrl(configUrl);

      while (true)
      {
         _Result result = _fetchAndLookup(configUrl, mac);
         const char* missingKey = nullptr;
         if (result == _Result::OK)
         {
            missingKey = _findMissingKey(requiredKeys);
            if (missingKey == nullptr)
            {
               return;
            }
         }

#ifdef ARDUINO_DISPLAY_SUPPORTED
         if ((result == _Result::MAC_NOT_FOUND || missingKey != nullptr) && arduino != nullptr)
         {
            arduino->clearDisplay();
            arduino->setTextSize(_HEADING_TEXT_SIZE);
            arduino->println(missingKey == nullptr ? "Device Not Registered" : "Device Config Incomplete", Color::HEADING);
            arduino->println();
            if (missingKey != nullptr)
            {
               arduino->println("Missing field:", Color::LABEL);
               arduino->println(missingKey, Color::VALUE);
               arduino->println();
            }
            arduino->println("Add an entry to:", Color::LABEL);
            arduino->println(configFileName.c_str(), Color::VALUE);
            arduino->println();
            arduino->println("MAC Address:", Color::LABEL);
            arduino->setTextSize(_MAC_TEXT_SIZE);
            arduino->println(mac.c_str(), Color::VALUE);
         }
#endif

         if (result == _Result::MAC_NOT_FOUND || missingKey != nullptr)
         {
            if (missingKey != nullptr)
            {
               Serial.print("Device Config Incomplete. Missing field: ");
               Serial.println(missingKey);
            }
            else
            {
               Serial.println("Device Not Registered.");
            }
            Serial.print("Add an entry to: ");
            Serial.println(configFileName);
            Serial.print("MAC Address: ");
            Serial.println(mac);
         }

         Serial.println("DeviceConfig: retrying...");
         delay(retryDelayMs);
      }
   }
};
