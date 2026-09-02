#pragma once

#include <stddef.h>

#include "ColorX.h"
#include "DisplayValue.h"
#include "Format.h"

///
/// <summary>
/// Common base for a single data series owned and rendered by a ScatterPlot. Holds the
/// display flags and colors shared by both concrete series types, and the common
/// single-value add()/clear() operations used to feed and reset a series regardless of
/// which plot implementation owns it.
/// </summary>
///
class IScatterPlotSeries
{
public:
   ///
   /// <summary>If true, draws each sample of this series.</summary>
   ///
   bool showPoints = true;

   ///
   /// <summary>If true, connects each drawn sample of this series to the previous one with a line.</summary>
   ///
   bool showLines = false;

   ///
   /// <summary>
   /// Size, in pixels, of each drawn point when showPoints is true. The default, 1, draws
   /// a single pixel. A size of 2 (or larger) instead draws a small plus-shaped marker
   /// centered on the point, spanning size pixels in each direction from the center.
   /// </summary>
   ///
   uint8_t pointSize = 1;

   ///
   /// <summary>If true, draws this series' centered moving average as a connected line.</summary>
   ///
   bool showMovingAverage = false;

   ///
   /// <summary>Color used to draw this series' moving-average line.</summary>
   ///
   Color movingAverageColor = Color::YELLOW;

   ///
   /// <summary>If true, draws a mean +/- stddev band for this series, recomputed each render().</summary>
   ///
   bool showStdDevBand = false;

   ///
   /// <summary>Color used to draw this series' stddev band.</summary>
   ///
   Color stdDevBandColor = Color::MAGENTA;

   ///
   /// <summary>Color used to draw this series' points and lines.</summary>
   ///
   Color color = Color::LIME;

   // Lazily-created DisplayValues for this series' mid-axis moving-average/stddev
   // labels, hoisted here so both concrete series types carry them uniformly instead of
   // each plot implementation needing its own parallel per-series bookkeeping struct.
   DisplayValue* movingAverageField = nullptr;
   DisplayValue* stdDevField = nullptr;

   virtual ~IScatterPlotSeries()
   {
      delete movingAverageField;
      delete stdDevField;
   }

   ///
   /// <summary>
   /// Adds a new value to the series.
   /// </summary>
   /// <param name="value">Value to add.</param>
   ///
   virtual void add(float value) = 0;

   ///
   /// <summary>
   /// Removes every retained sample from this series.
   /// </summary>
   ///
   virtual void clear() = 0;

   ///
   /// <summary>
   /// Prepares this series' render-facing buffers for the current frame (e.g.
   /// recomputing dirty moving-average/stddev caches, or refreshing a time-binned
   /// snapshot). Must be called once per render() pass before any of the bulk buffer
   /// accessors below are read.
   /// </summary>
   ///
   virtual void prepareForRender() = 0;

   ///
   /// <summary>
   /// Gets the number of valid entries in the buffers returned by xValues()/yValues().
   /// </summary>
   ///
   virtual size_t pointCount() const = 0;

   ///
   /// <summary>
   /// Gets a read-only view of this series' X values, valid until the next add() or
   /// prepareForRender() call. Has pointCount() valid entries.
   /// </summary>
   ///
   virtual const float* xValues() const = 0;

   ///
   /// <summary>
   /// Gets a read-only view of this series' raw Y values, parallel to xValues().
   /// </summary>
   ///
   virtual const float* yValues() const = 0;

   ///
   /// <summary>
   /// Gets a read-only view of this series' centered moving-average values, parallel to
   /// xValues(), or nullptr if not available (e.g. showMovingAverage is false or no data
   /// has been computed yet). NAN entries indicate no value at that index.
   /// </summary>
   ///
   virtual const float* movingAverageValues() const = 0;

   ///
   /// <summary>
   /// Gets a read-only view of this series' rolling stddev-band low values, parallel to
   /// xValues(), or nullptr if not available. NAN entries indicate no value at that index.
   /// </summary>
   ///
   virtual const float* stdDevLowValues() const = 0;

   ///
   /// <summary>
   /// Gets a read-only view of this series' rolling stddev-band high values, parallel to
   /// xValues(), or nullptr if not available. NAN entries indicate no value at that index.
   /// </summary>
   ///
   virtual const float* stdDevHighValues() const = 0;

   ///
   /// <summary>
   /// Gets this series' fixed X-axis range, if it has one (e.g. a time-binned series is
   /// always [-historyMs, 0] regardless of data). Series without a fixed range (e.g. a
   /// plain growing series) return false and the plot instead scans xValues() for a range.
   /// </summary>
   /// <param name="outMin">Receives the fixed minimum X value, if this returns true.</param>
   /// <param name="outMax">Receives the fixed maximum X value, if this returns true.</param>
   /// <returns>True if this series has a fixed X range.</returns>
   ///
   virtual bool getFixedXRange(float* outMin, float* outMax) const = 0;

   ///
   /// <summary>
   /// Gets the raw X/Y range of this series' currently displayed points (as of the last
   /// prepareForRender() call), used by ScatterPlot::_computeAxisRange() to fit the
   /// shared axis range to whatever is actually visible. A series with a fixed X range
   /// (see getFixedXRange()) still reports its actual Y range here since only X is fixed.
   /// </summary>
   /// <param name="outXMin">Receives the minimum finite X value found, or NAN if none exists.</param>
   /// <param name="outXMax">Receives the maximum finite X value found, or NAN if none exists.</param>
   /// <param name="outYMin">Receives the minimum finite Y value found, or NAN if none exists.</param>
   /// <param name="outYMax">Receives the maximum finite Y value found, or NAN if none exists.</param>
   ///
   virtual void getRawRange(float* outXMin, float* outXMax, float* outYMin, float* outYMax) = 0;

   ///
   /// <summary>
   /// Gets the min/max range of this series' centered moving average (as of the last
   /// prepareForRender() call).
   /// </summary>
   /// <param name="outMin">Receives the minimum finite value found, or NAN if none exists.</param>
   /// <param name="outMax">Receives the maximum finite value found, or NAN if none exists.</param>
   ///
   virtual void movingAverageRange(float* outMin, float* outMax) = 0;

   ///
   /// <summary>
   /// Gets the min/max range of this series' rolling mean +/- stddev band (as of the last
   /// prepareForRender() call).
   /// </summary>
   /// <param name="outMin">Receives the minimum finite value found, or NAN if none exists.</param>
   /// <param name="outMax">Receives the maximum finite value found, or NAN if none exists.</param>
   ///
   virtual void stdDevBandRange(float* outMin, float* outMax) = 0;

   ///
   /// <summary>
   /// Gets the most recently computed moving-average value (the latest point's centered
   /// window average), or NAN if unavailable.
   /// </summary>
   ///
   virtual float getLatestMovingAverage() = 0;

   ///
   /// <summary>
   /// Gets the most recently computed rolling stddev (half the width of the latest
   /// point's mean +/- stddev band), or NAN if unavailable.
   /// </summary>
   ///
   virtual float getLatestStdDev() = 0;
};
