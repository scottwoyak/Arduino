#pragma once

#include <NTPClient.h>

/// <summary>
/// Timer that triggers once per day using an NTP-based clock.
/// </summary>
/// <remarks>
/// Detects when the calendar day changes and signals when a new day begins.
/// Requires a synchronized NTPClient for time reference.
/// </remarks>
class DailyTimer
{
private:
   NTPClient* _clock;
   int _lastSavedDay;

   /// <summary>
   /// Gets the current day number from the NTP clock.
   /// </summary>
   /// <returns>Day count in epoch time</returns>
   int _getDay() const
   {
      return _clock->getEpochTime() / (24 * 60 * 60);
   }

public:
   /// <summary>
   /// Constructs a DailyTimer with the specified NTP clock.
   /// </summary>
   /// <param name="clock">Pointer to an NTPClient for time reference</param>
   explicit DailyTimer(NTPClient* clock)
   {
      _clock = clock;
   }

   /// <summary>
   /// Initializes the timer by capturing the current day.
   /// </summary>
   void begin()
   {
      _lastSavedDay = _getDay();
   }

   /// <summary>
   /// Checks if a new day has begun since the last check.
   /// </summary>
   /// <returns>true if a new day has begun; false otherwise</returns>
   bool ready()
   {
      int today = _getDay();
      if (today != _lastSavedDay)
      {
         _lastSavedDay = today;
         return true;
      }

      return false;
   }
};
