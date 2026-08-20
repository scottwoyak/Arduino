#pragma once

#include <time.h>
#include <Arduino.h>

#include "Util.h"

///
/// <summary>
/// Reboots the device on a fixed calendar-day schedule, for long-term stability.
/// Currently reboots once per day; kept as its own class so the interval can be
/// changed (e.g. every N days) without affecting callers.
/// </summary>
///
class Rebooter
{
private:
   /// <summary>Day-of-year recorded by begin(); -1 until then.</summary>
   int _startDay = -1;

public:
   ///
   /// <summary>
   /// Records the current day (via NTP-synced wall-clock time) as the reboot baseline
   /// for loop(). Call once from setup(), after WiFi/time sync (e.g. after initWifi()
   /// or an InfluxDB begin() that syncs NTP), so loop() can detect when the calendar
   /// day advances.
   /// </summary>
   ///
   void begin()
   {
      time_t now = time(nullptr);
      _startDay = localtime(&now)->tm_yday;
   }

   ///
   /// <summary>
   /// Reboots the device (via Util::reset()) once the calendar day has advanced past
   /// the day recorded by begin(). Intended for periodic use from loop(). Does nothing
   /// if begin() has not been called.
   /// </summary>
   ///
   void loop()
   {
      if (_startDay < 0)
      {
         return;
      }

      time_t now = time(nullptr);
      if (localtime(&now)->tm_yday != _startDay)
      {
         Serial.println("Performing scheduled daily reboot");
         Util::reset();
      }
   }
};
