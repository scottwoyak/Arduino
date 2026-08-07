#pragma once

#include <Arduino.h>
#include <time.h>

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
};
