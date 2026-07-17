#pragma once

#include <Arduino.h>
#include "RollingAverage.h"

///
/// <summary>
/// Rolling average that rejects incoming values falling outside the current
/// average +/- a filter band before adding them to the window.
/// </summary>
/// <remarks>
/// When the filter is 0, all finite values are accepted (no filtering is applied).
/// </remarks>
///
class FilteredRollingAverage : public RollingAverage
{
public:
   ///
   /// <summary>
   /// Selects how the filter value is interpreted when computing the allowed band around
   /// the current average.
   /// </summary>
   ///
   enum class FilterMode
   {
      /// <summary>The filter is an absolute value, added/subtracted directly from the average.</summary>
      VALUE,
      /// <summary>The filter is a percentage of the current average.</summary>
      PERCENT,
   };

private:
   float _filter;
   FilterMode _filterMode;

public:
   ///
   /// <summary>
   /// Initializes a filtered rolling average tracker with the specified window size and filter.
   /// </summary>
   /// <param name="size">Number of samples retained in the rolling window.</param>
   /// <param name="filter">Maximum allowed deviation from the current average. A value of 0 disables filtering.</param>
   /// <param name="filterMode">How to interpret the filter value.</param>
   ///
   explicit FilteredRollingAverage(size_t size, float filter = 0.0f, FilterMode filterMode = FilterMode::VALUE)
      : RollingAverage(size), _filter(filter), _filterMode(filterMode)
   {
   }

   FilteredRollingAverage(const FilteredRollingAverage&) = delete;
   FilteredRollingAverage& operator=(const FilteredRollingAverage&) = delete;

   ///
   /// <summary>
   /// Sets the filter value and mode used to reject outlier samples.
   /// </summary>
   /// <param name="filter">Maximum allowed deviation from the current average. A value of 0 disables filtering.</param>
   /// <param name="filterMode">How to interpret the filter value.</param>
   ///
   void setFilter(float filter, FilterMode filterMode = FilterMode::VALUE)
   {
      _filter = filter;
      _filterMode = filterMode;
   }

   ///
   /// <summary>
   /// Gets the current filter value.
   /// </summary>
   /// <returns>Maximum allowed deviation from the current average, or 0 when filtering is disabled.</returns>
   ///
   float filter() const
   {
      return _filter;
   }

   ///
   /// <summary>
   /// Gets the current filter mode.
   /// </summary>
   /// <returns>How the filter value is interpreted.</returns>
   ///
   FilterMode filterMode() const
   {
      return _filterMode;
   }

   ///
   /// <summary>
   /// Adds a value to the rolling window unless it is rejected by the filter.
   /// </summary>
   /// <param name="value">Value to append.</param>
   /// <returns>True when the value is added; false when the value is rejected by the filter or size is zero.</returns>
   ///
   bool set(float value)
   {
      if (_filter > 0.0f && isfinite(value))
      {
         float avg = average();
         if (isfinite(avg))
         {
            float band = (_filterMode == FilterMode::PERCENT) ? fabsf(avg) * (_filter / 100.0f) : _filter;
            if (value < avg - band || value > avg + band)
            {
               return false;
            }
         }
      }

      return RollingAverage::set(value);
   }
};
