#pragma once

#include <string>
#include "Color.h"

///
/// <summary>
/// Common interface implemented by histogram plot renderers (see HistogramPlot and
/// TimedHistogramPlotBase), so calling code can render and configure either kind of
/// histogram plot the same way regardless of which one is in use.
/// </summary>
///
class IHistogramPlot
{
public:
   virtual ~IHistogramPlot() = default;

   ///
   /// <summary>
   /// Renders the histogram to the display with incremental updates. Call this method
   /// repeatedly to update the display as new data arrives.
   /// </summary>
   ///
   virtual void render() = 0;

   ///
   /// <summary>
   /// Sets the color used to draw the Y-axis min/max labels.
   /// </summary>
   /// <param name="color">The color to use for axis labels.</param>
   ///
   virtual void setAxisLabelColor(Color color) = 0;

   ///
   /// <summary>
   /// Sets the format used to render the X-axis min/max value labels (and, for timed
   /// histograms, the sample range label) and forces them to be redrawn on the next render.
   /// </summary>
   /// <param name="format">Format string to apply to the X-axis labels.</param>
   ///
   virtual void setXAxisFormat(const char* format) = 0;

   ///
   /// <summary>
   /// Sets the format used to render the X-axis min/max value labels (and, for timed
   /// histograms, the sample range label) and forces them to be redrawn on the next render.
   /// </summary>
   /// <param name="format">Format string to apply to the X-axis labels.</param>
   ///
   void setXAxisFormat(const std::string& format)
   {
      setXAxisFormat(format.c_str());
   }

   ///
   /// <summary>
   /// Sets the format used to render the Y-axis min/max bin count labels and forces them
   /// to be redrawn on the next render.
   /// </summary>
   /// <param name="format">Format string to apply to the Y-axis labels.</param>
   ///
   virtual void setYAxisFormat(const char* format) = 0;

   ///
   /// <summary>
   /// Sets the format used to render the Y-axis min/max bin count labels and forces them
   /// to be redrawn on the next render.
   /// </summary>
   /// <param name="format">Format string to apply to the Y-axis labels.</param>
   ///
   void setYAxisFormat(const std::string& format)
   {
      setYAxisFormat(format.c_str());
   }

   ///
   /// <summary>
   /// Sets the color used to draw the histogram bars.
   /// </summary>
   /// <param name="color">The color to use for histogram bars.</param>
   ///
   virtual void setBarColor(Color color) = 0;
};
