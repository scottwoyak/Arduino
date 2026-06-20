#pragma once

#include <cmath>
#include <cstddef>

/// <summary>
/// Represents running statistics of floating-point values.
/// </summary>
/// <remarks>
/// The average is represented as a floating-point value. When no values have
/// been added, get() returns NaN. add() and remove() ignore NaN and infinite inputs.
/// </remarks>
class Stats
{
private:
   float    _avg   = NAN;
   size_t   _count = 0;
   float    _min   = NAN;
   float    _max   = NAN;

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
		 _avg = value;
			_min = value;
			_max = value;
	  }
	  else
	  {
		 _avg = (_avg * _count + value) / (_count + 1);

			if (std::isfinite(_min) && value < _min)
			{
				_min = value;
			}

			if (std::isfinite(_max) && value > _max)
			{
				_max = value;
			}
	  }

	  _count++;
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
		 _avg = NAN;
			_min = NAN;
			_max = NAN;
	  }
	  else if (_count == 1)
	  {
		 _avg   = NAN;
		 _count = 0;
			_min = NAN;
			_max = NAN;
	  }
	  else
	  {
		 _avg = (_avg * _count - value) / (_count - 1);
		 _count--;

			if (value == _min || value == _max)
			{
				_min = NAN;
				_max = NAN;
			}
	  }
	}

   /// <summary>
   /// Gets the current average.
   /// </summary>
   /// <returns>The running average as a float. Returns NaN if no values are present.</returns>
   float get() const
   {
	  return _avg;
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
	  _avg = NAN;
	  _count = 0;
		_min = NAN;
		_max = NAN;
	}
};
