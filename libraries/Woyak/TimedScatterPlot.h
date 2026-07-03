#pragma once

#include <new>
#include <Arduino.h>

#include "Feather.h"
#include "TimedValues.h"

/// <summary>
/// Renders a shaded scatter plot from a TimedValues buffer.
/// </summary>
class TimedScatterPlot
{
private:
   Feather& _feather;
   TimedValues& _samples;
   Format _minMaxFormat;
   Format _sampleRangeFormat;
   unsigned long _historyMs;
   float _minValueStep = 0.0f;

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

   float _alignToSensorStep(float value) const
   {
      if (_minValueStep <= 0.0f)
      {
         return value;
      }

      return roundf(value / _minValueStep) * _minValueStep;
   }

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
   /// </summary>
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
   /// Updates the visible time span.
   /// </summary>
   void setHistoryMs(unsigned long historyMs)
   {
      _historyMs = (historyMs == 0) ? 1 : historyMs;
   }

   void setMinMaxFormat(const Format& format)
   {
      _minMaxFormat = format;
   }

   void setSampleRangeFormat(const Format& format)
   {
      _sampleRangeFormat = format;
   }

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

         const unsigned long nowMs = millis();
         const bool sameChart = _hasDensityFrame && _densityChartWidth == chartWidth && _densityChartHeight == chartHeight;
         const float rangeEpsilon = (_minValueStep > 0.0f) ? (_minValueStep * 0.5f) : 0.001f;
         const bool rangeChanged = _hasDensityFrame
            && (fabsf(minValue - _densityMinValue) > rangeEpsilon || fabsf(maxValue - _densityMaxValue) > rangeEpsilon);
         const bool rebuildDensity = !_hasDensityFrame || !sameChart || rangeChanged;

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

            for (size_t i = 0; i < valueCount; i++)
            {
               float value = _sampleValues[i];
               if (alignToSensorStep)
               {
                  value = _alignToSensorStep(value);
               }

               if (value < plotMin)
               {
                  value = plotMin;
               }
               else if (value > plotMax)
               {
                  value = plotMax;
               }

               unsigned long boundedAgeMs = min(_sampleAgesMs[i], _historyMs - 1UL);
               int16_t x = static_cast<int16_t>(((_historyMs - 1UL - boundedAgeMs) * static_cast<unsigned long>(chartWidth - 1)) / (_historyMs - 1UL));
               x = constrain(x, 0, chartWidth - 1);

               _addDensityForValue(x, value, plotMax, valuePerPixel, pixelsPerStep, chartWidth, chartHeight);
            }
         }
         else
         {
            const unsigned long elapsedMs = nowMs - _lastDensityUpdateMs;
            _pendingShiftMs += elapsedMs;

            const unsigned long msPerPixel = (_historyMs > 1UL)
               ? std::max<unsigned long>(1UL, (_historyMs - 1UL) / static_cast<unsigned long>(std::max<int16_t>(1, chartWidth - 1)))
               : 1UL;

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

               if (value < plotMin)
               {
                  value = plotMin;
               }
               else if (value > plotMax)
               {
                  value = plotMax;
               }

               unsigned long boundedAgeMs = min(ageMs, _historyMs - 1UL);
               int16_t x = static_cast<int16_t>(((_historyMs - 1UL - boundedAgeMs) * static_cast<unsigned long>(chartWidth - 1)) / (_historyMs - 1UL));
               x = constrain(x, 0, chartWidth - 1);

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
