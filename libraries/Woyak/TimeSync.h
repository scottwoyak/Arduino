#pragma once

#include <Arduino.h>
#include <string>
#include <cstring>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

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
   /// Returns whether the system clock has already been synchronized (e.g. via sync() or
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
   /// public IP address via ip-api.com. Requires WiFi to already be connected.
   /// </summary>
   /// <param name="print">True to print lookup progress/result to Serial</param>
   /// <returns>The local UTC offset in seconds, or 0 (UTC) if the lookup fails.</returns>
   ///
   static long _fetchUtcOffsetSecs(bool print)
   {
      HTTPClient http;
      http.begin("http://ip-api.com/json/?fields=status,message,timezone,offset");
      int httpCode = http.GET();

      if (httpCode != HTTP_CODE_OK)
      {
         if (print)
         {
            Serial.print("TimeSync: timezone lookup failed, code: ");
            Serial.println(httpCode);
         }
         http.end();
         return 0;
      }

      String payload = http.getString();
      http.end();

      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (error)
      {
         if (print)
         {
            Serial.print("TimeSync: timezone lookup parse failed: ");
            Serial.println(error.c_str());
         }
         return 0;
      }

      const char* status = doc["status"] | "";
      if (strcmp(status, "success") != 0)
      {
         if (print)
         {
            Serial.print("TimeSync: timezone lookup failed: ");
            Serial.println(doc["message"].as<const char*>());
         }
         return 0;
      }

      long utcOffsetSecs = doc["offset"] | 0L;

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
