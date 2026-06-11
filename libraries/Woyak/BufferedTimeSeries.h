#pragma once

#include <Arduino.h>
#include <cmath>
#include "Util.h"

/// <summary>
/// A time-series buffer that stores timestamped sensor readings over a sliding time window.
/// When constructed with a time window (e.g. 2000 ms) the buffer keeps samples within that window.
/// get() returns the linearly interpolated value at the midpoint (half the window) in the past.
/// </summary>
class BufferedTimeSeries
{
private:
   /// <summary>
   /// Holds a single timestamped sensor reading.
   /// </summary>
   struct DataPoint
   {
      unsigned long time;
      float value;
   };

   DataPoint* _data = nullptr;
   size_t _bufferSize = 0;
   size_t _head = 0;   // next write index
   size_t _count = 0;  // number of valid points currently in buffer

   unsigned long _timeWindowMs = 1000; // total window length in ms
   unsigned int _resolutionMs = 50;    // nominal sampling resolution (used to size buffer)

public:
   /// <summary>
   /// Initializes a new instance specifying the time window to retain and an optional resolution.
   /// </summary>
   /// <param name="timeWindowMs">Sliding window size in milliseconds (e.g. 2000)</param>
   /// <param name="resolutionMs">Approximate expected sampling resolution in ms used to size the internal buffer</param>
   BufferedTimeSeries(unsigned long timeWindowMs, unsigned int resolutionMs)
      : _timeWindowMs(timeWindowMs), _resolutionMs(resolutionMs)
   {
      if (_resolutionMs == 0) _resolutionMs = 1;
      // compute buffer size to hold the expected number of samples within the time window
      size_t estimated = (_timeWindowMs + _resolutionMs - 1) / _resolutionMs; // ceil
      _bufferSize = (estimated < 2) ? 2 : (estimated + 2); // ensure room for interpolation
      _data = new DataPoint[_bufferSize];
      _head = 0;
      _count = 0;
   }

   // prevent shallow copies which would double-free the buffer
   BufferedTimeSeries(const BufferedTimeSeries&) = delete;
   BufferedTimeSeries& operator=(const BufferedTimeSeries&) = delete;

   /// <summary>
   /// Releases allocated resources.
   /// </summary>
   ~BufferedTimeSeries()
   {
      delete[] _data;
   }

   void _trim()
   {
      unsigned long now = millis();

      // drop old entries that are older than the time window
      while (_count > 1)
      {
         size_t oldestIdx = (_head + _bufferSize - _count) % _bufferSize;
         unsigned long oldestTime = _data[oldestIdx].time;
         unsigned long span = Util::getSpan(oldestTime, now);
         if (span > _timeWindowMs)
         {
            // advance oldest (drop it)
            _count--;
         }
         else
         {
            break;
         }
      }
   }

   /// <summary>
   /// Pushes a new reading into the buffer stamped with millis(). Old samples outside the time window are dropped.
   /// </summary>
   void set(float value)
   {
      unsigned long now = millis();

      // write new datapoint
      _data[_head].time = now;
      _data[_head].value = value;
      _head = (_head + 1) % _bufferSize;
      if (_count < _bufferSize) _count++;

      _trim();
   }

   /// <summary>
   /// Returns the interpolated value evaluated at the midpoint of the retained time window (timeWindowMs/2 ago).
   /// If not enough samples exist, returns the most recent sample or 0 if empty.
   /// </summary>
   float get() const
   {
      if (_count == 0) return NAN;

      size_t oldestIdx = (_head + _bufferSize - _count) % _bufferSize;
      size_t newestIdx = (_head + _bufferSize - 1) % _bufferSize;

      unsigned long oldestTime = _data[oldestIdx].time;
      unsigned long newestTime = _data[newestIdx].time;

      // target is midpoint of the configured time window relative to now
      unsigned long halfWindow = _timeWindowMs / 2;
      unsigned long now = millis();
      unsigned long targetTime = now - halfWindow;

      // If target is outside stored range, return NaN
      if (targetTime < oldestTime || targetTime > newestTime)
      {
         return NAN;
      }

      // find interval that contains targetTime
      for (size_t i = 0; i < _count - 1; i++)
      {
         size_t idxCurr = (oldestIdx + i) % _bufferSize;
         size_t idxNext = (idxCurr + 1) % _bufferSize;

         unsigned long t1 = _data[idxCurr].time;
         unsigned long t2 = _data[idxNext].time;

         unsigned long span12 = Util::getSpan(t1, t2);
         unsigned long span1T = Util::getSpan(t1, targetTime);

         if (span1T <= span12)
         {
            if (span1T == 0) return _data[idxCurr].value;
            if (span12 == 0) return _data[idxCurr].value;

            float frac = (float)span1T / (float)span12;
            return _data[idxCurr].value + frac * (_data[idxNext].value - _data[idxCurr].value);
         }
      }

      // fallback to newest value
      return _data[newestIdx].value;
   }

   bool ready() 
   { 
      _trim();

      if (_count == 0) return false;

      size_t oldestIdx = (_head + _bufferSize - _count) % _bufferSize;
      size_t newestIdx = (_head + _bufferSize - 1) % _bufferSize;

      unsigned long oldestTime = _data[oldestIdx].time;
      unsigned long newestTime = _data[newestIdx].time;

      // target is midpoint of the configured time window relative to now
      unsigned long halfWindow = _timeWindowMs / 2;
      unsigned long now = millis();
      unsigned long targetTime = now - halfWindow;

      // If target is outside stored range, return NaN
      if (targetTime < oldestTime || targetTime > newestTime)
      {
         return false;
      }

      return true;
   }

   /// <summary>
   /// Returns how many samples are currently stored.
   /// </summary>
   size_t count() const { return _count; }
};
