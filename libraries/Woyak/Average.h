#pragma once

#include <cmath>
#include <cstddef>

/// <summary>
/// Represents a running average of floating-point values.
/// </summary>
/// <remarks>
/// The average is represented as a floating-point value. When no values have
/// been added, get() returns NaN. add() and remove() ignore NaN and infinite inputs.
/// </remarks>
class Average
{
private:
   float    _avg   = NAN;
   size_t   _count = 0;

public:
   /// <summary>
   /// Initializes a new instance of the Average class.
   /// </summary>
   Average()
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
	  }
	  else
	  {
		 _avg = (_avg * _count + value) / (_count + 1);
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
	  }
	  else if (_count == 1)
	  {
		 _avg   = NAN;
		 _count = 0;
	  }
	  else
	  {
		 _avg = (_avg * _count - value) / (_count - 1);
		 _count--;
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
   }
};
