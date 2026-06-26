#pragma once

#include <Arduino.h>
#include "Tick.h"

/// <summary>
/// Specifies the precision and underlying unit used by <see cref="StopwatchT"/>.
/// </summary>
enum StopwatchPrecision
{
   /// <summary>
   /// Uses microsecond ticks from <c>micros()</c>.
   /// </summary>
   Micros,

   /// <summary>
   /// Uses millisecond ticks from <c>millis()</c>.
   /// </summary>
   Millis,

   /// <summary>
   /// Uses high-resolution ticks from <see cref="Tick::now"/>.
   /// </summary>
   Ticks,
};

/// <summary>
/// Represents a function that returns monotonically increasing timer ticks.
/// </summary>
using StopwatchTickSource = uint64_t (*)();

/// <summary>
/// Lightweight stopwatch for elapsed-time measurement.
/// </summary>
/// <typeparam name="TimerFunc">
/// Optional custom timer function returning monotonically increasing ticks.
/// When omitted, the timer source is selected from <see cref="StopwatchPrecision"/>.
/// </typeparam>
template<StopwatchTickSource TimerFunc = nullptr>
class StopwatchT
{
private:
   static constexpr double MICROS_PER_MILLI = 1000.0;
   static constexpr double MILLIS_PER_SEC = 1000.0;

   static uint64_t _microsTicks()
   {
      return static_cast<uint64_t>(micros());
   }

   static uint64_t _millisTicks()
   {
      return static_cast<uint64_t>(millis());
   }

   StopwatchPrecision _precision = StopwatchPrecision::Micros;
   uint64_t _startTicks = 0;
   uint64_t _elapsedTicks = 0;
   bool _running = false;

   uint64_t _currentTicks() const
   {
      if (_running)
      {
         return (_ticks() - _startTicks) + _elapsedTicks;
      }

      return _elapsedTicks;
   }

   StopwatchTickSource _ticks = nullptr;

   uint64_t _ticksFromMillis(double elapsedMillis) const
   {
      if (_precision == StopwatchPrecision::Micros || _precision == StopwatchPrecision::Ticks)
      {
         return static_cast<uint64_t>(elapsedMillis * MICROS_PER_MILLI);
      }

      return static_cast<uint64_t>(elapsedMillis);
   }

   unsigned long _ticksToMicros(uint64_t ticks) const
   {
      if (_precision == StopwatchPrecision::Micros)
      {
         return static_cast<unsigned long>(ticks);
      }

      if (_precision == StopwatchPrecision::Millis)
      {
         return static_cast<unsigned long>(MICROS_PER_MILLI * ticks);
      }

      return static_cast<unsigned long>(Tick::elapsedMicros(0, ticks));
   }

   double _ticksToMillis(uint64_t ticks) const
   {
      if (_precision == StopwatchPrecision::Micros)
      {
         return ticks / MICROS_PER_MILLI;
      }

      if (_precision == StopwatchPrecision::Millis)
      {
         return static_cast<double>(ticks);
      }

      return Tick::elapsedMillis(0, ticks);
   }

   /// <summary>
   /// Resolves the active time source based on template timer function and precision.
   /// </summary>
   StopwatchTickSource _resolveTicksSource() const
   {
      if (TimerFunc != nullptr)
      {
         return TimerFunc;
      }

      switch (_precision)
      {
      case StopwatchPrecision::Micros:
         return _microsTicks;
      case StopwatchPrecision::Millis:
         return _millisTicks;
      case StopwatchPrecision::Ticks:
      default:
         return Tick::now;
      }
   }

public:
   /// <summary>
   /// Initializes a new stopwatch with optional auto-start behavior.
   /// </summary>
   /// <param name="autoStart">
   /// True to start timing immediately; otherwise starts in a stopped state.
   /// </param>
   /// <param name="precision">The precision used for elapsed-time calculations.</param>
   StopwatchT(bool autoStart = true, StopwatchPrecision precision = StopwatchPrecision::Micros)
      : _precision(precision),
        _ticks(_resolveTicksSource())
   {
      if (autoStart)
      {
         start();
      }
   }

   /// <summary>
   /// Initializes and starts the stopwatch with the specified precision.
   /// Precision impacts the maximum measurable duration; microsecond precision
   /// has a lower maximum range than millisecond precision.
   /// </summary>
   /// <param name="precision">The underlying precision used to read elapsed time.</param>
   StopwatchT(StopwatchPrecision precision) : StopwatchT(true, precision)
   {
   }

   /// <summary>
   /// Gets the configured stopwatch precision.
   /// </summary>
   /// <returns>The active <see cref="StopwatchPrecision"/> value.</returns>
   StopwatchPrecision getPrecision() const
   {
      return _precision;
   }

   /// <summary>
   /// Starts timing if the stopwatch is not already running.
   /// </summary>
   void start()
   {
      if (!_running)
      {
         _running = true;
         _startTicks = _ticks();
      }
   }

   /// <summary>
   /// Stops timing and accumulates elapsed ticks.
   /// </summary>
   void stop()
   {
      if (_running)
      {
         _elapsedTicks += (_ticks() - _startTicks);
         _running = false;
      }
   }

   /// <summary>
   /// Resets elapsed time to the specified value in milliseconds.
   /// </summary>
   /// <param name="elapsedMillis">The new elapsed time value in milliseconds.</param>
   void reset(double elapsedMillis = 0)
   {
      _elapsedTicks = _ticksFromMillis(elapsedMillis);

      if (_running)
      {
         _startTicks = _ticks();
      }
   }

   /// <summary>
   /// Indicates whether the stopwatch is currently running.
   /// </summary>
   /// <returns>True if running; otherwise false.</returns>
   bool isRunning() const
   {
      return _running;
   }

   /// <summary>
   /// Gets elapsed time in microseconds.
   /// </summary>
   /// <returns>The elapsed time in microseconds.</returns>
   unsigned long elapsedMicros() const
   {
      return _ticksToMicros(_currentTicks());
   }

   /// <summary>
   /// Gets elapsed time in milliseconds.
   /// </summary>
   /// <returns>The elapsed time in milliseconds.</returns>
   double elapsedMillis() const
   {
      return _ticksToMillis(_currentTicks());
   }

   /// <summary>
   /// Gets elapsed time in seconds.
   /// </summary>
   /// <returns>The elapsed time in seconds.</returns>
   double elapsedSecs() const
   {
      return elapsedMillis() / MILLIS_PER_SEC;
   }

   /// <summary>
   /// Prints elapsed microseconds with a label.
   /// </summary>
   /// <param name="label">The text label printed before the value.</param>
   /// <param name="doReset">True to reset the stopwatch after printing.</param>
   void printlnMicros(const char* label, bool doReset = false)
   {
      Serial.print(label);
      Serial.print(" ");
      Serial.print(elapsedMicros());
      Serial.println(" micros");

      if (doReset)
      {
         reset();
      }
   }

   /// <summary>
   /// Prints elapsed milliseconds with a label.
   /// </summary>
   /// <param name="label">The text label printed before the value.</param>
   /// <param name="doReset">True to reset the stopwatch after printing.</param>
   void printlnMillis(const char* label, bool doReset = false)
   {
      Serial.print(label);
      Serial.print(" ");
      Serial.print(elapsedMillis());
      Serial.println("ms");

      if (doReset)
      {
         reset();
      }
   }

   /// <summary>
   /// Prints elapsed seconds with a label.
   /// </summary>
   /// <param name="label">The text label printed before the value.</param>
   /// <param name="doReset">True to reset the stopwatch after printing.</param>
   void printlnSecs(const char* label, bool doReset = false)
   {
      Serial.print(label);
      Serial.print(" ");
      Serial.print(elapsedSecs());
      Serial.println("s");

      if (doReset)
      {
         reset();
      }
   }
};

/// <summary>
/// Default stopwatch that selects timer source by precision.
/// </summary>
using Stopwatch = StopwatchT<>;
