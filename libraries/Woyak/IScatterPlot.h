#pragma once

#include <stddef.h>

#include "Color.h"
#include "Format.h"

///
/// <summary>
/// Common base for a single data series owned and rendered by an IScatterPlot
/// implementation (ScatterPlot or TimedScatterPlot). Holds the display flags and colors
/// shared by both concrete series types, and the common single-value add()/clear()
/// operations used to feed and reset a series regardless of which plot implementation
/// owns it.
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
   Color color = Color::GREEN;

   virtual ~IScatterPlotSeries() = default;

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
};

///
/// <summary>
/// Common interface implemented by both ScatterPlot and TimedScatterPlot, so callers can
/// work with either plot type interchangeably: create/manage series, add data (via each
/// series' IScatterPlotSeries::add()), and render, without depending on which concrete
/// plot implementation is in use. Both implementations are constructed from a fixed Rect16
/// screen rectangle, and both support showing a moving average and a moving stddev band
/// per series (see IScatterPlotSeries).
/// </summary>
///
class IScatterPlot
{
public:
   virtual ~IScatterPlot() = default;

   ///
   /// <summary>
   /// Creates a new series owned by this plot, using this plot implementation's default
   /// sizing (e.g. initial capacity or bin count).
   /// </summary>
   /// <returns>Pointer to the newly created series.</returns>
   ///
   virtual IScatterPlotSeries* createSeries() = 0;

   ///
   /// <summary>
   /// Gets a series by index.
   /// </summary>
   /// <param name="index">Index of the series to retrieve.</param>
   /// <returns>Pointer to the series at the specified index, or nullptr if index is invalid.</returns>
   ///
   virtual IScatterPlotSeries* getSeries(size_t index) = 0;

   ///
   /// <summary>
   /// Gets the number of series currently managed by this plot.
   /// </summary>
   /// <returns>Number of series.</returns>
   ///
   virtual size_t getSeriesCount() const = 0;

   ///
   /// <summary>
   /// Deletes a series at the specified index.
   /// </summary>
   /// <param name="index">Index of the series to delete.</param>
   ///
   virtual void deleteSeries(size_t index) = 0;

   ///
   /// <summary>
   /// Deletes all series managed by this plot.
   /// </summary>
   ///
   virtual void deleteAllSeries() = 0;

   ///
   /// <summary>
   /// Sets a custom format for the Y axis min/max labels.
   /// </summary>
   /// <param name="format">Format object defining precision and alignment.</param>
   ///
   virtual void setYAxisFormat(const Format& format) = 0;

   ///
   /// <summary>
   /// Sets the colors used to draw the chart's non-data elements. Forces a full redraw on
   /// the next render() since the plot-area background is repainted immediately.
   /// </summary>
   /// <param name="outerBackgroundColor">Color used outside the plotted-data area (e.g. behind axis labels/title).</param>
   /// <param name="plotAreaBackgroundColor">Color used behind the plotted-data area itself.</param>
   /// <param name="axisColor">Color used to draw the axis lines.</param>
   /// <param name="labelColor">Color used to draw axis labels and the title.</param>
   ///
   virtual void setColors(Color outerBackgroundColor, Color plotAreaBackgroundColor, Color axisColor, Color labelColor) = 0;

   ///
   /// <summary>
   /// Forces the next render() call to perform a full clear and redraw of the chart area.
   /// </summary>
   ///
   virtual void invalidate() = 0;

   ///
   /// <summary>
   /// Immediately erases any previously rendered chart content and clears every owned
   /// series, forcing the next render() to perform a full redraw.
   /// </summary>
   ///
   virtual void clear() = 0;

   ///
   /// <summary>
   /// Renders every owned series within this plot's fixed rectangle.
   /// </summary>
   ///
   virtual void render() = 0;
};
