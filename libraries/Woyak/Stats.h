#pragma once

#include <cmath>
#include <cstddef>

/// <summary>
/// Represents running statistics of floating-point values.
/// </summary>
/// <remarks>
/// When no values have been added, get() returns NaN. add() and remove()
/// ignore NaN and infinite inputs.
/// </remarks>
class Stats
{
private:
	size_t _count = 0;
	float _mean = NAN;
	float _m2 = 0.0f;
	float _min = NAN;
	float _max = NAN;

public:
	/// <summary>
	/// Initializes a new instance of the Stats class.
	/// </summary>
	Stats()
	{
	}

	/// <summary>
	/// Adds a value to the running average.
	/// </summary>
	/// <param name="value">The floating-point value to include in the average. NaN or infinite values are ignored.</param>
	void add(float value)
	{
		if (!std::isfinite(value))
		{
			return;
		}

		if (_count == 0)
		{
			_mean = value;
			_m2 = 0.0f;
			_min = value;
			_max = value;
			_count = 1;
			return;
		}

		float previousMean = _mean;
		_count++;
		_mean += (value - previousMean) / _count;
		_m2 += (value - previousMean) * (value - _mean);

		if (std::isfinite(_min) && value < _min)
		{
			_min = value;
		}

		if (std::isfinite(_max) && value > _max)
		{
			_max = value;
		}
	}

	/// <summary>
	/// Removes a value from the running average. Caller must ensure the value
	/// was previously added; this method simply updates the internal average
	/// assuming that is the case. NaN or infinite inputs are ignored.
	/// </summary>
	/// <param name="value">The floating-point value to remove from the average.</param>
	void remove(float value)
	{
		if (!std::isfinite(value))
		{
			return;
		}

		if (_count == 0)
		{
			reset();
			return;
		}

		if (_count == 1)
		{
			reset();
			return;
		}

		size_t previousCount = _count;
		float previousMean = _mean;
		_count--;
		_mean = (previousMean * previousCount - value) / _count;
		_m2 -= (value - previousMean) * (value - _mean);

		if (_m2 < 0.0f)
		{
			_m2 = 0.0f;
		}

		if (value == _min || value == _max)
		{
			_min = NAN;
			_max = NAN;
		}
	}

	/// <summary>
	/// Gets the current average.
	/// </summary>
	/// <returns>The running average as a float. Returns NaN if no values are present.</returns>
	float get() const
	{
		return (_count > 0) ? _mean : NAN;
	}

	/// <summary>
	/// Gets the minimum value currently tracked.
	/// </summary>
	/// <returns>The minimum value, or NaN when empty or when range is unknown after removals.</returns>
	float min() const
	{
		if (_count == 0 || !std::isfinite(_min))
		{
			return NAN;
		}

		return _min;
	}

	/// <summary>
	/// Gets the maximum value currently tracked.
	/// </summary>
	/// <returns>The maximum value, or NaN when empty or when range is unknown after removals.</returns>
	float max() const
	{
		if (_count == 0 || !std::isfinite(_max))
		{
			return NAN;
		}

		return _max;
	}

	/// <summary>
	/// Gets the running population variance.
	/// </summary>
	/// <returns>The variance, or NaN when no values are present.</returns>
	float variance() const
	{
		if (_count == 0)
		{
			return NAN;
		}

		float variance = _m2 / _count;
		if (variance < 0.0f)
		{
			variance = 0.0f;
		}

		return variance;
	}

	/// <summary>
	/// Gets the running population standard deviation.
	/// </summary>
	/// <returns>The standard deviation, or NaN when no values are present.</returns>
	float stdDev() const
	{
		float currentVariance = variance();
		return std::isfinite(currentVariance) ? sqrtf(currentVariance) : NAN;
	}

	/// <summary>
	/// Alias for stdDev().
	/// </summary>
	/// <returns>The standard deviation, or NaN when no values are present.</returns>
	float sigma() const
	{
		return stdDev();
	}

	/// <summary>
	/// Gets the number of values currently included in the average.
	/// </summary>
	/// <returns>The count of values.</returns>
	size_t count() const
	{
		return _count;
	}

	/// <summary>
	/// Resets the running average to an empty state.
	/// </summary>
	void reset()
	{
		_mean = NAN;
		_m2 = 0.0f;
		_count = 0;
		_min = NAN;
		_max = NAN;
	}
};
