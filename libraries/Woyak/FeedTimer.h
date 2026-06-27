#pragma once

#include <NTPClient.h>

/// <summary>
/// Timer that triggers at regular intervals synchronized to absolute clock times.
/// </summary>
/// <remarks>
/// Ensures uploads occur at predictable times (e.g., on the minute, every 10 minutes)
/// rather than arbitrary intervals. Useful for data collection aligned with calendar/clock times.
/// Requires an NTPClient for synchronization.
/// </remarks>
class FeedTimer
{
private:
   NTPClient* _clock;
   unsigned long _millisAtNextSave;
   unsigned long _intervalSecs;
   unsigned long _offsetSecs;
   bool _first = true;

   /// <summary>
   /// Synchronizes the next save time with the network clock.
   /// </summary>
   /// <remarks>
   /// Calculates when the next interval-aligned event should occur based on
   /// the current time of day from the NTPClient.
   /// </remarks>
   void _syncWithClock()
   {
      // Get the current time of day in total seconds
      int hours = _clock->getHours();
      int mins = _clock->getMinutes();
      int secs = _clock->getSeconds();
      long currentSecs = 60 * 60 * hours + 60 * mins + secs;

      // Figure out the next round time value (aligned to interval boundaries)
      long desiredSecs = 0;
      while (desiredSecs <= currentSecs)
      {
         desiredSecs += _intervalSecs;
      }
      desiredSecs += _offsetSecs;

      // Calculate the milliseconds until the next aligned time
      long deltaMillis = 1000 * (desiredSecs - currentSecs);
      _millisAtNextSave = millis() + deltaMillis;
   }

public:
   /// <summary>
   /// Constructs a FeedTimer with the specified interval and offset.
   /// </summary>
   /// <param name="clock">Pointer to an NTPClient for time reference</param>
   /// <param name="intervalSecs">Interval between uploads in seconds (e.g., 60 for every minute, 600 for every 10 minutes)</param>
   /// <param name="uploadImmediately">If true, trigger immediately; otherwise wait for next interval</param>
   /// <param name="offsetSecs">Offset in seconds within the interval (0 is on the hour, etc.)</param>
   FeedTimer(
      NTPClient* clock,
      unsigned long intervalSecs,
      bool uploadImmediately = true,
      unsigned long offsetSecs = 0)
   {
      _clock = clock;
      _intervalSecs = intervalSecs;
      _first = uploadImmediately;
      _offsetSecs = offsetSecs;
   }

   /// <summary>
   /// Initializes the timer by synchronizing with the network clock.
   /// </summary>
   void begin()
   {
      _syncWithClock();
   }

   /// <summary>
   /// Checks if it is time to perform the next upload.
   /// </summary>
   /// <returns>true if upload is due; false otherwise</returns>
   /// <remarks>
   /// When true is returned, the next trigger time is automatically calculated.
   /// </remarks>
   bool ready()
   {
      if (_first)
      {
         _first = false;
         return true;
      }

      if (millis() > _millisAtNextSave)
      {
         _syncWithClock();
         return true;
      }

      return false;
   }

   /// <summary>
   /// Gets the milliseconds remaining until the next upload.
   /// </summary>
   /// <returns>Milliseconds until next save, or 0 if past due</returns>
   unsigned long msUntilNextSave()
   {
      unsigned long now = millis();
      if (_millisAtNextSave > now)
      {
         return _millisAtNextSave - now;
      }

      return 0;
   }
};
