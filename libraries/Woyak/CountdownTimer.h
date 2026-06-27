#pragma once

#include "Stopwatch.h"

/// <summary>
/// Timer that counts down from a specified duration.
/// </summary>
/// <remarks>
/// Tracks elapsed time and provides remaining time queries. Can be initialized with
/// duration in milliseconds or seconds. Automatically starts on construction.
/// </remarks>
class CountdownTimer
{
   Stopwatch _sw;
   unsigned long _timerMs;

public:
   /// <summary>
   /// Constructs a CountdownTimer with the specified duration in milliseconds.
   /// </summary>
   /// <param name="durationMs">Duration in milliseconds</param>
   explicit CountdownTimer(unsigned long durationMs)
      : _sw(StopwatchPrecision::Millis)
   {
      _timerMs = durationMs;
   }

   /// <summary>
   /// Constructs a CountdownTimer with the specified duration in seconds.
   /// </summary>
   /// <param name="durationS">Duration in seconds (float)</param>
   explicit CountdownTimer(float durationS)
      : CountdownTimer(static_cast<unsigned long>(1000.0f * durationS))
   {
   }

   /// <summary>
   /// Resets the timer to its initial state.
   /// </summary>
   void reset()
   {
      _sw.reset();
   }

   /// <summary>
   /// Gets the remaining time in milliseconds.
   /// </summary>
   /// <returns>Remaining milliseconds, or 0 if expired</returns>
   unsigned long remainingMs() const
   {
      if (isExpired())
      {
         return 0;
      }

      return static_cast<unsigned long>(ceil(_timerMs - _sw.elapsedMillis()));
   }

   /// <summary>
   /// Gets the remaining time in seconds.
   /// </summary>
   /// <returns>Remaining seconds as a double, or 0 if expired</returns>
   double remainingS() const
   {
      return remainingMs() / 1000.0;
   }

   /// <summary>
   /// Determines whether the countdown has expired.
   /// </summary>
   /// <returns>true if the timer has expired; false otherwise</returns>
   bool isExpired() const
   {
      return _sw.elapsedMillis() > _timerMs;
   }
};
