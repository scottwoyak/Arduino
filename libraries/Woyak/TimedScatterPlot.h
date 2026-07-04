#pragma once

#include <new>
#include <Arduino.h>

#include "Feather.h"
#include "TimedValues.h"

///
/// <summary>
/// Renders a shaded scatter plot from a TimedValues buffer with time-windowed history.
/// Points are displayed with density shading to show sample concentration.
/// Incremental rendering updates only changed pixels for efficiency.
/// </summary>
///
class TimedScatterPlot
{
private:
   Feather& _feather;
   TimedValues& _samples;
   Format _minMaxFormat;
   Format _sampleRangeFormat;
   unsigned long _historyMs;
   float _minValueStep = 0.0f;

   int16_t* _previousBarHeights = nullptr;
   uint _previousRenderedBins = 0;
   float* _sampleValues = nullptr;
   unsigned long* _sampleAgesMs = nullptr;
   size_t _sampleCapacity = 0;

   uint8_t* _density = nullptr;
   uint16_t* _previousPixels = nullptr;
   size_t _pixelCapacity = 0;

   unsigned long _lastRenderMicros = 0;
   unsigned long _lastComputeMicros = 0;
   unsigned long _lastDisplayMicros = 0;

   int16_t _lastShiftPixels = 0;
   size_t _lastAddedSamples = 0;
   bool _lastRebuiltDensity = false;
   unsigned long _lastAddWindowMs = 0;

   bool _hasDensityFrame = false;
   float _densityMinValue = NAN;
   float _densityMaxValue = NAN;
   int16_t _densityChartWidth = 0;
   int16_t _densityChartHeight = 0;
   unsigned long _lastDensityUpdateMs = 0;
   unsigned long _pendingShiftMs = 0;

   /// <summary>
   /// Ensures sample value and age buffers are allocated with sufficient capacity.
   /// </summary>
   /// <param name="capacity">Required capacity in elements.</param>
   /// <returns>True if allocation succeeded or was already sufficient; false on failure.</returns>
   bool _ensureSampleBuffers(size_t capacity)
   {
      if (capacity <= _sampleCapacity)
      {
         return true;
      }

      float* sampleValues = new (std::nothrow) float[capacity];
      unsigned long* sampleAgesMs = new (std::nothrow) unsigned long[capacity];
      if (sampleValues == nullptr || sampleAgesMs == nullptr)
      {
         delete[] sampleValues;
         delete[] sampleAgesMs;
         return false;
      }

      delete[] _sampleValues;
      delete[] _sampleAgesMs;
      _sampleValues = sampleValues;
      _sampleAgesMs = sampleAgesMs;
      _sampleCapacity = capacity;
      return true;
   }

   bool _ensurePixelBuffers(size_t capacity)
   {
      if (capacity == _pixelCapacity)
      {
         return true;
      }

      uint8_t* density = new (std::nothrow) uint8_t[capacity];
      uint16_t* previousPixels = new (std::nothrow) uint16_t[capacity];
      if (density == nullptr || previousPixels == nullptr)
      {
         delete[] density;
         delete[] previousPixels;
         return false;
      }

      delete[] _density;
      delete[] _previousPixels;
      _density = density;
      _previousPixels = previousPixels;
      _pixelCapacity = capacity;

      memset(_density, 0, _pixelCapacity);
      for (size_t i = 0; i < _pixelCapacity; i++)
      {
         _previousPixels[i] = static_cast<uint16_t>(Color::BLACK);
      }

      return true;
   }

   /// <summary>
   /// Clears pixels that have changed from their previous color to black.
   /// Used during incremental rendering updates.
   /// </summary>
   /// <param name="chartLeft">Left x-coordinate of the chart area.</param>
   /// <param name="chartTop">Top y-coordinate of the chart area.</param>
   /// <param name="chartWidth">Width in pixels of the chart area.</param>
   /// <param name="chartHeight">Height in pixels of the chart area.</param>
   void _clearChangedPixels(int16_t chartLeft, int16_t chartTop, int16_t chartWidth, int16_t chartHeight)
   {
      if (_previousPixels == nullptr)
      {
         return;
      }

      size_t pixelCount = static_cast<size_t>(chartWidth) * static_cast<size_t>(chartHeight);
      for (size_t i = 0; i < pixelCount; i++)
      {
         if (_previousPixels[i] == static_cast<uint16_t>(Color::BLACK))
         {
            continue;
         }

         int16_t x = chartLeft + static_cast<int16_t>(i % static_cast<size_t>(chartWidth));
         int16_t y = chartTop + static_cast<int16_t>(i / static_cast<size_t>(chartWidth));
         _feather.display.writePixel(x, y, static_cast<uint16_t>(Color::BLACK));
         _previousPixels[i] = static_cast<uint16_t>(Color::BLACK);
      }
   }

   /// <summary>
   /// Shifts density array left by the specified pixel count.
   /// </summary>
   /// <param name="chartWidth">Width in pixels of the chart area.</param>
   /// <param name="chartHeight">Height in pixels of the chart area.</param>
   /// <param name="shiftPixels">Number of pixels to shift left.</param>
   void _shiftDensityLeft(int16_t chartWidth, int16_t chartHeight, int16_t shiftPixels)
   {
      if (shiftPixels <= 0 || shiftPixels >= chartWidth)
      {
         return;
      }

      const size_t copyCount = static_cast<size_t>(chartWidth - shiftPixels);
      for (int16_t y = 0; y < chartHeight; y++)
      {
         uint8_t* row = _density + static_cast<size_t>(y) * static_cast<size_t>(chartWidth);
         memmove(row, row + shiftPixels, copyCount);
         memset(row + copyCount, 0, static_cast<size_t>(shiftPixels));
      }
   }

   float _axisPrecisionScale() const
   {
      float scale = 1.0f;
      const uint8_t precision = _minMaxFormat.precision();
      for (uint8_t i = 0; i < precision; i++)
      {
         scale *= 10.0f;
      }
      return scale;
   }

   /// <summary>
   /// Rounds a value down to the axis label precision.
   /// </summary>
   /// <param name="value">Value to round.</param>
   /// <returns>Value rounded down to axis precision.</returns>
   float _roundDownToAxisPrecision(float value) const
   {
      const float scale = _axisPrecisionScale();
      return floorf(value * scale) / scale;
   }

   /// <summary>
   /// Rounds a value up to the axis label precision.
   /// </summary>
   /// <param name="value">Value to round.</param>
   /// <returns>Value rounded up to axis precision.</returns>
   float _roundUpToAxisPrecision(float value) const
   {
      const float scale = _axisPrecisionScale();
      return ceilf(value * scale) / scale;
   }

   /// <summary>
   /// Computes the epsilon (smallest meaningful difference) for the axis precision.
   /// </summary>
   /// <returns>Epsilon value at the axis precision level.</returns>
   float _axisPrecisionEpsilon() const
   {
      const float scale = _axisPrecisionScale();
      return 0.5f / scale;
   }

   /// <summary>
   /// Aligns a value to the sensor step size if one is configured.
   /// </summary>
   /// <param name="value">Value to align.</param>
   /// <returns>Value aligned to sensor step size, or original value if step is not configured.</returns>
   float _alignToSensorStep(float value) const
   {
      if (_minValueStep <= 0.0f)
      {
         return value;
      }

      return roundf(value / _minValueStep) * _minValueStep;
   }

   /// <summary>
   /// Increments density at a single pixel location.
   /// </summary>
   /// <param name="x">Pixel x-coordinate within the chart.</param>
   /// <param name="y">Pixel y-coordinate within the chart.</param>
   /// <param name="chartWidth">Width in pixels of the chart area.</param>
   /// <param name="chartHeight">Height in pixels of the chart area.</param>
   void _addDensityPixel(int16_t x, int16_t y, int16_t chartWidth, int16_t chartHeight)
   {
      if (x < 0 || x >= chartWidth || y < 0 || y >= chartHeight)
      {
         return;
      }

      size_t densityIndex = static_cast<size_t>(y) * static_cast<size_t>(chartWidth) + static_cast<size_t>(x);
      if (_density[densityIndex] < 255)
      {
         _density[densityIndex]++;
      }
      _lastAddedSamples++;
   }

   /// <summary>
   /// Clamps a value to the specified plot range.
   /// </summary>
   /// <param name="value">Value to clamp.</param>
   /// <param name="plotMin">Minimum value in the plot range.</param>
   /// <param name="plotMax">Maximum value in the plot range.</param>
   /// <returns>Clamped value.</returns>
   float _clampToPlotRange(float value, float plotMin, float plotMax) const
   {
      if (value < plotMin)
      {
         return plotMin;
      }
      if (value > plotMax)
      {
         return plotMax;
      }
      return value;
   }

   /// <summary>
   /// Converts an age in milliseconds to an x-coordinate on the chart.
   /// </summary>
   /// <param name="ageMs">Age of the sample in milliseconds.</param>
   /// <param name="chartWidth">Width in pixels of the chart area.</param>
   /// <returns>X-coordinate for the sample (newer samples on the right).</returns>
   int16_t _xForAgeMs(unsigned long ageMs, int16_t chartWidth) const
   {
      if (_historyMs <= 1UL || chartWidth <= 1)
      {
         return chartWidth - 1;
      }

      unsigned long boundedAgeMs = min(ageMs, _historyMs - 1UL);
      int16_t x = static_cast<int16_t>(((_historyMs - 1UL - boundedAgeMs) * static_cast<unsigned long>(chartWidth - 1)) / (_historyMs - 1UL));
      return constrain(x, 0, chartWidth - 1);
   }

   /// <summary>
   /// Adds density pixels for a sample value at a given x-coordinate.
   /// Spans multiple pixels if the sensor resolution is coarser than pixel resolution.
   /// </summary>
   /// <param name="x">X-coordinate on the chart.</param>
   /// <param name="value">Sample value to plot.</param>
   /// <param name="plotMax">Maximum value in the plot range.</param>
   /// <param name="valuePerPixel">Y-axis scale (value units per pixel).</param>
   /// <param name="pixelsPerStep">Width of each sensor step in pixels.</param>
   /// <param name="chartWidth">Width in pixels of the chart area.</param>
   /// <param name="chartHeight">Height in pixels of the chart area.</param>
   void _addDensityForValue(int16_t x, float value, float plotMax, float valuePerPixel, float pixelsPerStep, int16_t chartWidth, int16_t chartHeight)
   {
      if (valuePerPixel <= 0.0f)
      {
         return;
      }

      const float yFloat = (plotMax - value) / valuePerPixel;
      if (pixelsPerStep <= 1.0f)
      {
         int16_t y = static_cast<int16_t>(roundf(yFloat));
         _addDensityPixel(x, y, chartWidth, chartHeight);
         return;
      }

      const float halfSpan = (pixelsPerStep - 1.0f) * 0.5f;
      int16_t yStart = static_cast<int16_t>(floorf(yFloat - halfSpan));
      int16_t yEnd = static_cast<int16_t>(ceilf(yFloat + halfSpan));

      for (int16_t y = yStart; y <= yEnd; y++)
      {
         _addDensityPixel(x, y, chartWidth, chartHeight);
      }
   }

public:
   /// <summary>
   /// Creates a timed scatter plot renderer using default formats.
   /// Default min/max format shows one decimal place right-aligned.
   /// Default sample range format shows one decimal place with 's' suffix, center-aligned.
   /// </summary>
   /// <param name="feather">Reference to the Feather display interface.</param>
   /// <param name="samples">Reference to the TimedValues buffer containing samples.</param>
   /// <param name="historyMs">Time window in milliseconds for the visible history.</param>
   /// <param name="minValueStep">Optional sensor resolution step size (0.0 for no alignment, default 0.0).</param>
   TimedScatterPlot(Feather& feather, TimedValues& samples, unsigned long historyMs, float minValueStep = 0.0f)
      : TimedScatterPlot(feather, samples, historyMs, Format("##.#", Format::Alignment::RIGHT), Format("##.#s", Format::Alignment::CENTER), minValueStep)
   {}

   /// <summary>
   /// Creates a timed scatter plot renderer with custom formats.
   /// </summary>
   TimedScatterPlot(Feather& feather, TimedValues& samples, unsigned long historyMs, const Format& minMaxFormat, const Format& sampleRangeFormat, float minValueStep = 0.0f)
      : _feather(feather), _samples(samples), _minMaxFormat(minMaxFormat), _sampleRangeFormat(sampleRangeFormat), _historyMs(historyMs), _minValueStep(minValueStep)
   {
      if (_historyMs == 0)
      {
         _historyMs = 1;
      }
   }

   TimedScatterPlot(const TimedScatterPlot&) = delete;
   TimedScatterPlot& operator=(const TimedScatterPlot&) = delete;

   /// <summary>
   /// Releases the allocated rendering buffers.
   /// </summary>
   ~TimedScatterPlot()
   {
      delete[] _sampleValues;
      delete[] _sampleAgesMs;
      delete[] _density;
      delete[] _previousPixels;
   }

   /// <summary>
   /// Updates the visible time span in milliseconds.
   /// </summary>
   /// <param name="historyMs">Time window in milliseconds to display.</param>
   void setHistoryMs(unsigned long historyMs)
   {
      _historyMs = (historyMs == 0) ? 1 : historyMs;
   }

   /// <summary>
   /// Sets a custom format for axis min/max labels.
   /// </summary>
   /// <param name="format">Format object defining precision and alignment.</param>
   void setMinMaxFormat(const Format& format)
   {
      _minMaxFormat = format;
   }

   /// <summary>
   /// Sets a custom format for the sample range time display.
   /// </summary>
   /// <param name="format">Format object defining precision and alignment.</param>
   void setSampleRangeFormat(const Format& format)
   {
      _sampleRangeFormat = format;
   }

   /// <summary>
   /// Gets the time in microseconds of the last render operation.
   /// </summary>
   /// <returns>Time elapsed during last render in microseconds.</returns>
   unsigned long lastRenderMicros() const
   {
      return _lastRenderMicros;
   }

   unsigned long lastComputeMicros() const
   {
      return _lastComputeMicros;
   }

    unsigned long lastDisplayMicros() const
    {
       return _lastDisplayMicros;
    }

   int16_t lastShiftPixels() const
   {
      return _lastShiftPixels;
   }

   size_t lastAddedSamples() const
   {
      return _lastAddedSamples;
   }

   bool lastRebuiltDensity() const
   {
      return _lastRebuiltDensity;
   }

   unsigned long lastAddWindowMs() const
   {
      return _lastAddWindowMs;
   }

   /// <summary>
   /// Draws the scatter plot.
   /// </summary>
   void render()
   {
      const unsigned long frameStartMicros = micros();

      _feather.setTextSize(2);

      const int16_t width = _feather.width();
      const int16_t height = _feather.height();
      const int16_t infoHeight = _feather.charH();
      const int16_t labelWidth = static_cast<int16_t>(_minMaxFormat.length() * _feather.charW());
      const int16_t chartLeft = labelWidth + _feather.charW();
      const int16_t chartTop = infoHeight + 2;
      const int16_t chartWidth = width - chartLeft - 1;
      const int16_t rangeLineHeight = _feather.charH() + 2;
      const int16_t chartHeight = height - chartTop - rangeLineHeight;

      if (chartWidth < 2 || chartHeight < 2)
      {
         const unsigned long displayStartMicros = micros();
         _feather.display.startWrite();
         _feather.println("Plot area too small", Color::LABEL);
         _feather.display.endWrite();
         _lastDisplayMicros = micros() - displayStartMicros;
         _lastRenderMicros = micros() - frameStartMicros;
         _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
         return;
      }

      if (!_ensureSampleBuffers(_samples.size()))
      {
         const unsigned long displayStartMicros = micros();
         _feather.display.startWrite();
         _feather.println("Memory unavailable", Color::LABEL);
         _feather.display.endWrite();
         _lastDisplayMicros = micros() - displayStartMicros;
         _lastRenderMicros = micros() - frameStartMicros;
         _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
         return;
      }

      const size_t valueCount = _samples.snapshot(_sampleValues, _sampleAgesMs, _sampleCapacity);

      if (!_ensurePixelBuffers(static_cast<size_t>(chartWidth) * static_cast<size_t>(chartHeight)))
      {
         const unsigned long displayStartMicros = micros();
         _feather.display.startWrite();
         _feather.println("Memory unavailable", Color::LABEL);
         _feather.display.endWrite();
         _lastDisplayMicros = micros() - displayStartMicros;
         _lastRenderMicros = micros() - frameStartMicros;
         _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
         return;
      }

      _lastShiftPixels = 0;
      _lastAddedSamples = 0;
      _lastRebuiltDensity = false;
      _lastAddWindowMs = 0;

      float minValue = 0.0f;
      float maxValue = 0.0f;
      float axisMinValue = 0.0f;
      float axisMaxValue = 0.0f;
      float sampleRangeSeconds = NAN;

      if (valueCount > 0)
      {
         minValue = _sampleValues[0];
         maxValue = minValue;
         for (size_t i = 1; i < valueCount; i++)
         {
            const float value = _sampleValues[i];
            minValue = min(minValue, value);
            maxValue = max(maxValue, value);
         }

         minValue = _roundDownToAxisPrecision(minValue);
         maxValue = _roundUpToAxisPrecision(maxValue);

         const unsigned long nowMs = millis();
         const unsigned long msPerPixel = (_historyMs > 1UL)
            ? std::max<unsigned long>(1UL, (_historyMs - 1UL) / static_cast<unsigned long>(std::max<int16_t>(1, chartWidth - 1)))
            : 1UL;

         const bool sameChart = _hasDensityFrame && _densityChartWidth == chartWidth && _densityChartHeight == chartHeight;
         const float rangeEpsilon = _axisPrecisionEpsilon();
         const bool rangeChanged = _hasDensityFrame
            && (fabsf(minValue - _densityMinValue) > rangeEpsilon || fabsf(maxValue - _densityMaxValue) > rangeEpsilon);
         const bool staleDensity = _hasDensityFrame && ((nowMs - _lastDensityUpdateMs) > msPerPixel);
         const bool rebuildDensity = !_hasDensityFrame || !sameChart || rangeChanged || staleDensity;

         float plotMin = rebuildDensity ? minValue : _densityMinValue;
         float plotMax = rebuildDensity ? maxValue : _densityMaxValue;
         axisMinValue = plotMin;
         axisMaxValue = plotMax;

         float range = plotMax - plotMin;
         if (range <= 0.0f)
         {
            range = 1.0f;
         }

         const float valuePerPixel = range / static_cast<float>(std::max<int16_t>(1, chartHeight - 1));
         const bool alignToSensorStep = _minValueStep > 0.0f;
         const float pixelsPerStep = alignToSensorStep ? (_minValueStep / valuePerPixel) : 1.0f;

         if (rebuildDensity)
         {
            _lastRebuiltDensity = true;
            _pendingShiftMs = 0;
            memset(_density, 0, _pixelCapacity);
            for (size_t i = 0; i < _pixelCapacity; i++)
            {
               _previousPixels[i] = static_cast<uint16_t>(Color::BLACK);
            }

            for (size_t i = 0; i < valueCount; i++)
            {
               float value = _sampleValues[i];
               if (alignToSensorStep)
               {
                  value = _alignToSensorStep(value);
               }

               value = _clampToPlotRange(value, plotMin, plotMax);
               int16_t x = _xForAgeMs(_sampleAgesMs[i], chartWidth);
               _addDensityForValue(x, value, plotMax, valuePerPixel, pixelsPerStep, chartWidth, chartHeight);
            }
         }
         else
         {
            const unsigned long elapsedMs = nowMs - _lastDensityUpdateMs;
            _pendingShiftMs += elapsedMs;

            int16_t shiftPixels = static_cast<int16_t>(_pendingShiftMs / msPerPixel);
            if (shiftPixels > 1)
            {
               shiftPixels = 1;
            }

            if (shiftPixels <= 0)
            {
               _lastShiftPixels = 0;
               _lastAddWindowMs = 0;
               _lastDensityUpdateMs = nowMs;
               _lastRenderMicros = micros() - frameStartMicros;
               _lastComputeMicros = _lastRenderMicros;
               _lastDisplayMicros = 0;
               return;
            }

            _pendingShiftMs -= static_cast<unsigned long>(shiftPixels) * msPerPixel;
            _lastShiftPixels = shiftPixels;
            _shiftDensityLeft(chartWidth, chartHeight, shiftPixels);

            const unsigned long addWindowMs = msPerPixel;
            _lastAddWindowMs = addWindowMs;

            for (size_t i = 0; i < valueCount; i++)
            {
               const unsigned long ageMs = _sampleAgesMs[i];
               if (ageMs > addWindowMs)
               {
                  break;
               }

               float value = _sampleValues[i];
               if (alignToSensorStep)
               {
                  value = _alignToSensorStep(value);
               }

               value = _clampToPlotRange(value, plotMin, plotMax);
               int16_t x = _xForAgeMs(ageMs, chartWidth);
               _addDensityForValue(x, value, plotMax, valuePerPixel, pixelsPerStep, chartWidth, chartHeight);
            }
         }

         unsigned long maxAgeMs = 0;
         for (size_t i = 0; i < valueCount; i++)
         {
            maxAgeMs = max(maxAgeMs, _sampleAgesMs[i]);
         }

         sampleRangeSeconds = static_cast<float>(maxAgeMs) / 1000.0f;
         _hasDensityFrame = true;
         _densityMinValue = plotMin;
         _densityMaxValue = plotMax;
         _densityChartWidth = chartWidth;
         _densityChartHeight = chartHeight;
         _lastDensityUpdateMs = nowMs;
      }

      const unsigned long displayStartMicros = micros();

      _feather.display.startWrite();

      if (valueCount == 0)
      {
         _hasDensityFrame = false;
         _pendingShiftMs = 0;
         _clearChangedPixels(chartLeft, chartTop, chartWidth, chartHeight);
         _feather.display.endWrite();
         _lastDisplayMicros = micros() - displayStartMicros;
         _lastRenderMicros = micros() - frameStartMicros;
         _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
         return;
      }

      for (size_t i = 0; i < _pixelCapacity; i++)
      {
         uint16_t newColor = (_density[i] > 0)
            ? static_cast<uint16_t>(Color::GREEN)
            : static_cast<uint16_t>(Color::BLACK);

         if (_previousPixels[i] == newColor)
         {
            continue;
         }

         int16_t x = chartLeft + static_cast<int16_t>(i % static_cast<size_t>(chartWidth));
         int16_t y = chartTop + static_cast<int16_t>(i / static_cast<size_t>(chartWidth));
         _feather.display.writePixel(x, y, newColor);
         _previousPixels[i] = newColor;
      }

      _feather.fillRect(chartLeft, chartTop + chartHeight - 1, chartWidth, 1, Color::DARKGRAY);
      _feather.fillRect(chartLeft, chartTop, 1, chartHeight, Color::DARKGRAY);

      _feather.setCursor(0, chartTop);
      _feather.print(axisMaxValue, _minMaxFormat, Color::LABEL);
      _feather.setCursor(0, chartTop + chartHeight - _feather.charH());
      _feather.print(axisMinValue, _minMaxFormat, Color::LABEL);

      if (isfinite(sampleRangeSeconds))
      {
         int16_t sampleRangeX = chartLeft + (chartWidth - static_cast<int16_t>(_sampleRangeFormat.length() * _feather.charW())) / 2;
         _feather.setCursor(sampleRangeX, chartTop + chartHeight + 1);
         _feather.print(sampleRangeSeconds, _sampleRangeFormat, Color::LABEL);
      }

      _feather.display.endWrite();

      _lastDisplayMicros = micros() - displayStartMicros;
      _lastRenderMicros = micros() - frameStartMicros;
      _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
   }
};
