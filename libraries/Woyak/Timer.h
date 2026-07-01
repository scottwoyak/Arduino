#pragma once

#include "Util.h"

/// <summary>
/// Base template for timers - maintains cadence (does not drift)
/// </summary>
/// <remarks>
/// This template timer automatically starts upon construction and maintains
/// a fixed cadence independent of execution delays. The next interval is 
/// aligned with the original start time, preventing timing drift over 
/// multiple cycles.
/// </remarks>
/// <typeparam name="TimeFunc">Function pointer returning current time</typeparam>
template<unsigned long(*TimeFunc)()>
class TimerBase
{
protected:
	unsigned long _startTime;     ///< Start time in time units
	unsigned long _durationTime;  ///< Interval duration in time units

public:
	/// <summary>
	/// Constructs a TimerBase with the specified duration.
	/// </summary>
	/// <param name="duration">Duration in time units</param>
	explicit TimerBase(unsigned long duration)
		: _durationTime(duration)
	{
		_startTime = TimeFunc();
	}

	/// <summary>
	/// Resets the timer start point to the current time.
	/// </summary>
	void reset()
	{
		_startTime = TimeFunc();
	}

	/// <summary>
	/// Checks if the timer interval has elapsed.
	/// </summary>
	/// <returns>true if interval has elapsed; false otherwise</returns>
	/// <remarks>
	/// When true is returned, the internal timer is advanced by one or more
	/// complete intervals, maintaining cadence for the next cycle.
	/// </remarks>
	bool ready()
	{
		if (_durationTime == 0)
		{
			_startTime = TimeFunc();
			return true;
		}

		unsigned long now = TimeFunc();
		unsigned long span = Util::getSpan(_startTime, now);

		if (span >= _durationTime)
		{
			unsigned long intervals = span / _durationTime;
			_startTime += intervals * _durationTime;
			return true;
		}
		return false;
	}

	/// <summary>
	/// Returns the remaining time until the next interval elapses.
	/// </summary>
	/// <returns>Remaining time in time units, or 0 if expired</returns>
	unsigned long remaining() const
	{
		if (_durationTime == 0)
		{
			return 0;
		}

		unsigned long now = TimeFunc();
		unsigned long span = Util::getSpan(_startTime, now);
		return (span >= _durationTime) ? 0 : (_durationTime - span);
	}
};

/// <summary>
/// Timer using milliseconds - maintains cadence (does not drift)
/// </summary>
/// <remarks>
/// This timer automatically starts upon construction and maintains a fixed
/// cadence independent of execution delays. The next interval is aligned
/// with the original start time, preventing timing drift over multiple cycles.
/// </remarks>
class TimerMillis : public TimerBase<millis>
{
public:
	/// <summary>
	/// Constructs a TimerMillis with the specified duration.
	/// </summary>
	/// <param name="durationMs">Duration in milliseconds</param>
	explicit TimerMillis(unsigned long durationMs)
		: TimerBase(durationMs) {}
};

/// <summary>
/// Timer using microseconds - maintains cadence (does not drift)
/// </summary>
/// <remarks>
/// This timer automatically starts upon construction and maintains a fixed
/// cadence independent of execution delays. The next interval is aligned
/// with the original start time, preventing timing drift over multiple cycles.
/// </remarks>
class TimerMicros : public TimerBase<micros>
{
public:
	/// <summary>
	/// Constructs a TimerMicros with the specified duration.
	/// </summary>
	/// <param name="durationUs">Duration in microseconds</param>
	explicit TimerMicros(unsigned long durationUs)
		: TimerBase(durationUs) {}
};

/// <summary>
/// Timer using seconds (float) - maintains cadence (does not drift)
/// </summary>
/// <remarks>
/// This timer accepts duration as a float in seconds and converts internally
/// to milliseconds for resolution. It automatically starts upon construction
/// and maintains a fixed cadence independent of execution delays. The next
/// interval is aligned with the original start time, preventing timing drift.
/// </remarks>
class TimerSecs : public TimerBase<millis>
{
public:
	/// <summary>
	/// Constructs a TimerSecs with the specified duration.
	/// </summary>
	/// <param name="durationSecs">Duration in seconds (float)</param>
	explicit TimerSecs(float durationSecs)
		: TimerBase(static_cast<unsigned long>(durationSecs * 1000.0f)) {}

	/// <summary>
	/// Returns the remaining time until the next interval elapses.
	/// </summary>
	/// <returns>Remaining time in seconds (float), or 0 if expired</returns>
	float remaining() const
	{
		return TimerBase::remaining() / 1000.0f;
	}
};

/// <summary>
/// Alias for TimerMillis for backward compatibility
/// </summary>
using Timer = TimerMillis;
