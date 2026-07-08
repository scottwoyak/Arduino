#pragma once

#include "ArduinoWithDisplay.h"
#include "Color.h"
#include "Format.h"
#include "Util.h"
#include <math.h>
#include <new>

///
/// <summary>
/// Renders a density-based scatter plot of sensor values on the display.
/// Automatically calculates min/max ranges and displays axis labels.
/// </summary>
///
class ScatterPlot
{
private:
   ArduinoWithDisplay* _display;
   int16_t _x;
   int16_t _y;
   int16_t _width;
   int16_t _height;
   int16_t _yAxisWidth;

   // State for the current overlay frame, established by beginOverlay() and used by
   // subsequent plotSeries() calls.
   int16_t _chartLeft = 0;
   int16_t _chartTop = 0;
   int16_t _chartWidth = 0;
   int16_t _chartHeight = 0;
   float _overlayYMin = 0.0f;
   float _overlayYMax = 0.0f;
   unsigned long _overlayXMaxMs = 0;

   uint8_t* _density = nullptr;
   uint16_t* _lineBuffer = nullptr;
   int16_t _lastPixelCount = 0;
   int16_t _lastChartWidth = 0;

public:
   ///
   /// <summary>
   /// Initializes a new ScatterPlot at the specified position and dimensions.
   /// </summary>
   /// <param name="display">Pointer to the display object.</param>
   /// <param name="x">X coordinate for the plot.</param>
   /// <param name="y">Y coordinate for the plot.</param>
   /// <param name="width">Width of the plot area in pixels.</param>
   /// <param name="height">Height of the plot area in pixels.</param>
   ///
   ScatterPlot(ArduinoWithDisplay* display, int16_t x, int16_t y, int16_t width, int16_t height)
      : _display(display), _x(x), _y(y), _width(width), _height(height), _yAxisWidth(50)
   {
   }

   ~ScatterPlot()
   {
      delete[] _density;
      delete[] _lineBuffer;
   }

   ///
   /// <summary>
   /// Prepares the plot area for an overlay of one or more colored series that share a
   /// common axis range: draws gray X/Y axis lines, the Y-axis min/max labels, and the
   /// X-axis "0s"/duration labels, and (optionally) clears the chart area. Call once per
   /// frame before any plotSeries() calls; the established geometry and axis range remain
   /// in effect for plotSeries() calls made on subsequent frames until beginOverlay() is
   /// called again.
   /// </summary>
   /// <param name="yMin">Minimum value of the shared Y-axis range.</param>
   /// <param name="yMax">Maximum value of the shared Y-axis range.</param>
   /// <param name="xMaxMs">Maximum elapsed time (milliseconds) of the shared X-axis range.</param>
   /// <param name="yAxisFormat">Format used to render the Y-axis min/max labels.</param>
   /// <param name="xAxisFormat">Format used to render the X-axis duration label (in seconds).</param>
   /// <param name="clearChart">If true, clears the chart area before plotting.</param>
   ///
   void beginOverlay(float yMin, float yMax, unsigned long xMaxMs, const Format& yAxisFormat, const Format& xAxisFormat, bool clearChart = true)
   {
      if (_display == nullptr)
      {
         return;
      }

      _display->setTextSize(2);

      int16_t labelWidth = static_cast<int16_t>(yAxisFormat.length() * _display->charW());
      _yAxisWidth = labelWidth + _display->charW();
      int16_t axisLineHeight = _display->charH() + 2;

      _chartLeft = _x + _yAxisWidth;
      _chartTop = _y;
      _chartWidth = _width - _yAxisWidth;
      _chartHeight = _height - axisLineHeight;

      _overlayYMin = yMin;
      _overlayYMax = yMax;
      _overlayXMaxMs = xMaxMs;

      if (_chartHeight < 20 || _chartWidth < 20)
      {
         return;
      }

      if (clearChart)
      {
         _display->fillRect(_x, _chartTop, _width, _height, Color::BLACK);
      }

      _display->fillRect(_chartLeft, _chartTop + _chartHeight - 1, _chartWidth, 1, Color::GRAY);
      _display->fillRect(_chartLeft, _chartTop, 1, _chartHeight, Color::GRAY);

      _display->setCursor(_x, _chartTop);
      _display->print(yMax, yAxisFormat, Color::WHITE);
      _display->setCursor(_x, _chartTop + _chartHeight - _display->charH());
      _display->print(yMin, yAxisFormat, Color::WHITE);

      int16_t axisLabelY = _chartTop + _chartHeight + 2;
      _display->setCursor(_chartLeft, axisLabelY);
      _display->print("0s", Color::WHITE);
      _display->setCursor(_x, axisLabelY);
      _display->printR(static_cast<float>(xMaxMs) / 1000.0f, xAxisFormat, Color::WHITE);
   }

   ///
   /// <summary>
   /// Plots one series' samples into the chart area established by the most recent
   /// beginOverlay() call, starting at the given index and excluding any point whose
   /// elapsed time falls beyond the shared X-axis range (e.g. once zoomed in). Passing a
   /// non-zero start index plots only newly added points, avoiding the need to redraw
   /// previously plotted points. When connectPoints is true, each plotted point is joined
   /// to the prior in-range point with a line instead of drawing a lone pixel; when
   /// startIndex is non-zero, the line segment still connects back to sample
   /// startIndex - 1 so a series plotted incrementally across frames has no gap.
   /// </summary>
   /// <param name="values">Sample values for the series.</param>
   /// <param name="elapsedMs">Elapsed times (parallel to values) for the series.</param>
   /// <param name="count">Number of samples in the series.</param>
   /// <param name="startIndex">Index of the first sample to plot.</param>
   /// <param name="color">Color used to plot this series' points.</param>
   /// <param name="connectPoints">If true, draws a line connecting each point to the previous one instead of a lone pixel.</param>
   ///
   void plotSeries(const float* values, const unsigned long* elapsedMs, size_t count, size_t startIndex, Color color, bool connectPoints = false) const
   {
      if (_display == nullptr || values == nullptr || count == 0 || _overlayXMaxMs == 0 || startIndex >= count)
      {
         return;
      }

      float ySpan = _overlayYMax - _overlayYMin;
      if (ySpan <= 0.0f)
      {
         ySpan = 1.0f;
      }

      auto toPixel = [&](size_t i, int16_t& x, int16_t& y)
      {
         x = _chartLeft + static_cast<int16_t>((static_cast<float>(elapsedMs[i]) / static_cast<float>(_overlayXMaxMs)) * (_chartWidth - 1));
         y = _chartTop + static_cast<int16_t>(((_overlayYMax - values[i]) / ySpan) * (_chartHeight - 1));

         x = constrain(x, _chartLeft, _chartLeft + _chartWidth - 1);
         y = constrain(y, _chartTop, _chartTop + _chartHeight - 1);
      };

      bool havePrevPoint = false;
      int16_t prevX = 0;
      int16_t prevY = 0;

      if (connectPoints && (startIndex > 0))
      {
         size_t prevIndex = startIndex - 1;
         if (isfinite(values[prevIndex]) && (elapsedMs[prevIndex] <= _overlayXMaxMs))
         {
            toPixel(prevIndex, prevX, prevY);
            havePrevPoint = true;
         }
      }

      for (size_t i = startIndex; i < count; i++)
      {
         float value = values[i];
         if (!isfinite(value) || (elapsedMs[i] > _overlayXMaxMs))
         {
            continue;
         }

         int16_t x;
         int16_t y;
         toPixel(i, x, y);

         if (connectPoints && havePrevPoint)
         {
            _display->drawLine(prevX, prevY, x, y, color);
         }
         else
         {
            _display->drawPixel(x, y, color);
         }

         prevX = x;
         prevY = y;
         havePrevPoint = true;
      }
   }

   ///
   /// <summary>
   /// Renders the scatter plot with the provided values.
   /// </summary>
   /// <param name="values">Array of sensor values to plot.</param>
   /// <param name="valueCount">Number of values in the array.</param>
   ///
   void render(const float* values, size_t valueCount)
   {
      if (_display == nullptr || values == nullptr || valueCount < 2)
      {
         return;
      }

      // Calculate min/max
      float plotYMin = values[0];
      float plotYMax = values[0];
      for (size_t i = 1; i < valueCount; i++)
      {
         float value = values[i];
         plotYMin = min(plotYMin, value);
         plotYMax = max(plotYMax, value);
      }

      if (!isfinite(plotYMin) || !isfinite(plotYMax))
      {
         return;
      }

      _display->setTextSize(2);
      // Format labels to 3 significant digits dynamically using Util::toSignificantString
      String maxLabelStr = Util::toSignificantString(plotYMax, 3);
      String minLabelStr = Util::toSignificantString(plotYMin, 3);
      int16_t yLabelChars = max(static_cast<int16_t>(maxLabelStr.length()), static_cast<int16_t>(minLabelStr.length()));
      int16_t yLabelPixelWidth = static_cast<int16_t>(yLabelChars * _display->charW());
      int16_t yAxisGap = 2;
      _yAxisWidth = yLabelPixelWidth + yAxisGap + 1;

      int16_t chartLeft = _x + _yAxisWidth;
      int16_t chartTop = _y;
      int16_t chartWidth = _width - _yAxisWidth;
      int16_t chartHeight = _height;

      if (chartHeight < 20 || chartWidth < 20)
      {
         return;
      }

      float ySpan = plotYMax - plotYMin;
      if (ySpan <= 0.0f)
      {
         ySpan = 1.0f;
      }

      // Create binary map
      size_t pixelCount = static_cast<size_t>(chartWidth) * static_cast<size_t>(chartHeight);
      if (pixelCount != _lastPixelCount || _density == nullptr)
      {
         delete[] _density;
         _density = new (std::nothrow) uint8_t[pixelCount];
         _lastPixelCount = pixelCount;
      }

      if (_density == nullptr)
      {
         Util::setHaltReason("OOM allocating density array in ScatterPlot");
         Util::reset();
         return;
      }
      memset(_density, 0, pixelCount);

      // Populate binary map
      float sampleXMultiplier = static_cast<float>(chartWidth - 1) / max(static_cast<size_t>(1), valueCount - 1);
      float sampleYMultiplier = static_cast<float>(chartHeight - 1) / ySpan;

      for (size_t sampleIndex = 0; sampleIndex < valueCount; sampleIndex++)
      {
         int16_t x = static_cast<int16_t>(sampleIndex * sampleXMultiplier);
         float value = values[sampleIndex];
         int16_t y = static_cast<int16_t>((plotYMax - value) * sampleYMultiplier);

         if (y < 0) y = 0;
         else if (y >= chartHeight) y = chartHeight - 1;

         size_t densityIndex = static_cast<size_t>(y) * static_cast<size_t>(chartWidth) + static_cast<size_t>(x);
         _density[densityIndex] = 1;
      }

      // Render chart using a line buffer to reduce flicker and speed up drawing
      if (chartWidth != _lastChartWidth || _lineBuffer == nullptr)
      {
         delete[] _lineBuffer;
         _lineBuffer = new (std::nothrow) uint16_t[chartWidth];
         _lastChartWidth = chartWidth;
      }

      if (_lineBuffer == nullptr)
      {
         Util::setHaltReason("OOM allocating lineBuffer in ScatterPlot");
         Util::reset();
         return;
      }

      uint16_t bgColor = static_cast<uint16_t>(Color::BLACK);
      uint16_t axisColor = static_cast<uint16_t>(Color::DARKGRAY);
      uint16_t greenColor = static_cast<uint16_t>(Color::GREEN);

      _display->display.setSwapBytes(true);
      uint8_t* densityPtr = _density;
      for (int16_t y = 0; y < chartHeight; y++)
      {
         bool isAxisY = (y == chartHeight - 1);
         for (int16_t x = 0; x < chartWidth; x++)
         {
            uint16_t pixelColor = (x == 0 || isAxisY) ? axisColor : bgColor;

            if (*densityPtr++ > 0)
            {
               pixelColor = greenColor;
            }

            _lineBuffer[x] = pixelColor;
         }

         _display->display.pushImage(chartLeft, chartTop + y, chartWidth, 1, _lineBuffer);
      }
      _display->display.setSwapBytes(false);

      // Display Y-axis labels (right-aligned)
      int16_t maxLabelX = chartLeft - yAxisGap - static_cast<int16_t>(maxLabelStr.length() * _display->charW());
      _display->setCursor(maxLabelX, chartTop);
      _display->print(maxLabelStr, Color::LABEL);

      int16_t minLabelX = chartLeft - yAxisGap - static_cast<int16_t>(minLabelStr.length() * _display->charW());
      _display->setCursor(minLabelX, chartTop + chartHeight - _display->charH());
      _display->print(minLabelStr, Color::LABEL);

      // Display X-axis labels
      _display->setCursor(chartLeft, chartTop + chartHeight + 1);
      _display->print("0", Color::LABEL);

      String xMaxLabel = String(static_cast<unsigned long>(valueCount - 1));
      int16_t xMaxX = chartLeft + chartWidth - static_cast<int16_t>(xMaxLabel.length() * _display->charW());
      if (xMaxX < chartLeft)
      {
         xMaxX = chartLeft;
      }
      _display->setCursor(xMaxX, chartTop + chartHeight + 1);
      _display->print(xMaxLabel, Color::LABEL);
   }
};
