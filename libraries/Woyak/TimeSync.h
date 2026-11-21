#pragma once

#include <Arduino.h>
#include <string>
#include <cstring>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>

#include "Logger.h"

///
/// <summary>
/// Utility for synchronizing the system clock via NTP. Replaces the timeSync() helper from
/// the ESP8266_Influxdb library so callers outside of Influx can reuse it and control whether
/// diagnostics are printed to Serial.
/// </summary>
///
class TimeSync
{
public:
   /// <summary>Maximum number of 500ms checks to wait for time synchronization.</summary>
   static constexpr uint8_t MAX_SYNC_CHECKS = 40;

   /// <summary>Delay between time sync checks in milliseconds.</summary>
   static constexpr uint16_t SYNC_CHECK_DELAY_MS = 500;

   /// <summary>Timeout in milliseconds for the timezone lookup HTTP request.</summary>
   static constexpr uint16_t HTTP_TIMEOUT_MS = 5000;

   ///
   /// <summary>
   /// Synchronizes the system clock using the given timezone and NTP servers, optionally
   /// printing progress and the resulting synchronized time to Serial.
   /// </summary>
   /// <param name="tzInfo">POSIX timezone string</param>
   /// <param name="ntpServer1">Primary NTP server</param>
   /// <param name="ntpServer2">Secondary NTP server</param>
   /// <param name="ntpServer3">Tertiary NTP server</param>
   /// <param name="print">True to print sync progress/result to Serial</param>
   ///
   static void sync(const char* tzInfo, const char* ntpServer1, const char* ntpServer2, const char* ntpServer3 = nullptr, bool print = false)
   {
      configTzTime(tzInfo, ntpServer1, ntpServer2, ntpServer3);

      _waitForSync(print);
   }

   ///
   /// <summary>
   /// Synchronizes the system clock via NTP, automatically determining the local UTC offset
   /// (including DST) from the device's public IP address via worldtimeapi.org. WiFi must
   /// already be connected before calling this. Falls back to UTC (no offset) if the
   /// timezone lookup fails, optionally printing progress and the resulting synchronized
   /// time to Serial.
   /// </summary>
   /// <param name="ntpServer1">Primary NTP server</param>
   /// <param name="ntpServer2">Secondary NTP server</param>
   /// <param name="ntpServer3">Tertiary NTP server</param>
   /// <param name="print">True to print sync progress/result to Serial</param>
   ///
   static void syncWithAutoTimezone(const char* ntpServer1, const char* ntpServer2, const char* ntpServer3 = nullptr, bool print = false)
   {
      long utcOffsetSecs = _fetchUtcOffsetSecs(print);

      configTime(utcOffsetSecs, 0, ntpServer1, ntpServer2, ntpServer3);

      _waitForSync(print);
   }

   ///
   /// <summary>
   /// Starts an NTP sync using an automatically determined timezone, but does not wait for
   /// it to complete (the SNTP client finishes in the background; poll isSynced()). Intended
   /// for retrying after a failed syncWithAutoTimezone() without blocking the caller.
   /// </summary>
   /// <param name="ntpServer1">Primary NTP server</param>
   /// <param name="ntpServer2">Secondary NTP server</param>
   ///
   static void startSyncWithAutoTimezone(const char* ntpServer1, const char* ntpServer2)
   {
      long utcOffsetSecs = _fetchUtcOffsetSecs(false);

      configTime(utcOffsetSecs, 0, ntpServer1, ntpServer2);
   }

   ///
   /// <summary>
   /// Returns whether the system clock has already been synchronized
   /// syncWithAutoTimezone()). Useful to avoid re-syncing (and re-printing progress) when a
   /// caller isn't sure whether the clock was already synced earlier during startup.
   /// </summary>
   /// <returns>True if the system clock is already synchronized.</returns>
   ///
   static bool isSynced()
   {
      return time(nullptr) >= 1000000000l;
   }

   ///
   /// <summary>
   /// Formats the current local time as a short 12-hour time, e.g. "9:45 AM".
   /// </summary>
   /// <returns>The formatted local time string.</returns>
   ///
   static std::string localTimeString()
   {
      time_t now = time(nullptr);
      struct tm timeInfo;
      localtime_r(&now, &timeInfo);

      char buffer[16];
      strftime(buffer, sizeof(buffer), "%I:%M %p", &timeInfo);

      // Strip a leading zero from the hour, e.g. "09:45 AM" -> "9:45 AM".
      const char* result = (buffer[0] == '0') ? buffer + 1 : buffer;
      return std::string(result);
   }

private:
   static constexpr const char* PREFERENCES_NAMESPACE = "Device";
   static constexpr const char* UTC_OFFSET_PREF_KEY = "utcOffset";

   ///
   /// <summary>
   /// Loads the last successfully fetched UTC offset (in seconds) from NVS, or 0 (UTC)
   /// if no offset has been cached yet.
   /// </summary>
   /// <returns>Cached UTC offset in seconds, or 0 if unavailable.</returns>
   ///
   static long _loadCachedUtcOffsetSecs()
   {
      Preferences preferences;
      if (preferences.begin(PREFERENCES_NAMESPACE, true))
      {
         long cached = preferences.getLong(UTC_OFFSET_PREF_KEY, 0L);
         preferences.end();
         return cached;
      }
      return 0L;
   }

   ///
   /// <summary>
   /// Persists the UTC offset (in seconds) in NVS when it differs from the cached value.
   /// </summary>
   /// <param name="utcOffsetSecs">UTC offset in seconds to cache.</param>
   ///
   static void _saveCachedUtcOffsetSecs(long utcOffsetSecs)
   {
      Preferences preferences;
      if (preferences.begin(PREFERENCES_NAMESPACE, false))
      {
         if (!preferences.isKey(UTC_OFFSET_PREF_KEY) ||
             preferences.getLong(UTC_OFFSET_PREF_KEY, 0L) != utcOffsetSecs)
         {
            preferences.putLong(UTC_OFFSET_PREF_KEY, utcOffsetSecs);
         }
         preferences.end();
      }
   }

   ///
   /// <summary>
   /// Waits for the system clock to become synchronized, optionally printing progress and
   /// the resulting synchronized time to Serial.
   /// </summary>
   /// <param name="print">True to print sync progress/result to Serial</param>
   ///
   static void _waitForSync(bool print)
   {
      if (print)
      {
         Serial.print("Syncing time");
      }

      uint8_t i = 0;
      while (time(nullptr) < 1000000000l && i < MAX_SYNC_CHECKS)
      {
         if (print)
         {
            Serial.print(".");
         }

         delay(SYNC_CHECK_DELAY_MS);
         i++;
      }

      if (print)
      {
         Serial.println();

         time_t tnow = time(nullptr);
         Serial.print("Synchronized time: ");
         Serial.println(ctime(&tnow));
      }
   }

   ///
   /// <summary>
   /// Looks up the local UTC offset (including any DST adjustment) for the device's current
   /// public IP address via ip-api.com. Requires WiFi to already be connected. Caches
   /// successful offsets in NVS and falls back to the cached offset (or 0/UTC if none is
   /// cached) on lookup failure.
   /// </summary>
   /// <param name="print">True to print lookup progress/result to Serial</param>
   /// <returns>The local UTC offset in seconds, or the cached offset / 0 if the lookup fails.</returns>
   ///
   static long _fetchUtcOffsetSecs(bool print)
   {
      if (WiFi.status() != WL_CONNECTED)
      {
         if (print)
         {
            Serial.println("TimeSync: timezone lookup skipped, WiFi not connected");
         }
         return _loadCachedUtcOffsetSecs();
      }

      HTTPClient http;
      http.setConnectTimeout(HTTP_TIMEOUT_MS);
      http.setTimeout(HTTP_TIMEOUT_MS);
      http.begin("http://ip-api.com/json/?fields=status,message,timezone,offset");
      int httpCode = http.GET();

      if (httpCode != HTTP_CODE_OK)
      {
         http.end();
         long cached = _loadCachedUtcOffsetSecs();
         Logger.log(
            "TimeSync: timezone lookup failed (HTTP " + String(httpCode) + "), using cached offset " + String(cached),
            LogSeverity::WARN,
            "Time");
         return cached;
      }

      String payload = http.getString();
      http.end();

      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (error)
      {
         long cached = _loadCachedUtcOffsetSecs();
         Logger.log(
            "TimeSync: timezone lookup parse failed (" + String(error.c_str()) + "), using cached offset " + String(cached),
            LogSeverity::WARN,
            "Time");
         return cached;
      }

      const char* status = doc["status"] | "";
      if (strcmp(status, "success") != 0)
      {
         long cached = _loadCachedUtcOffsetSecs();
         const char* message = doc["message"] | "unknown";
         Logger.log(
            "TimeSync: timezone lookup failed (" + String(message) + "), using cached offset " + String(cached),
            LogSeverity::WARN,
            "Time");
         return cached;
      }

      long utcOffsetSecs = doc["offset"] | 0L;
      _saveCachedUtcOffsetSecs(utcOffsetSecs);

      if (print)
      {
         Serial.print("TimeSync: timezone: ");
         Serial.print(doc["timezone"].as<const char*>());
         Serial.print(", UTC offset: ");
         Serial.println(utcOffsetSecs);
      }

      return utcOffsetSecs;
   }
};
