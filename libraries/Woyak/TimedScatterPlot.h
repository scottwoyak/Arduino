#pragma once

#include <new>
#include <Arduino.h>

#include "ArduinoWithDisplay.h"
#include "TimedValues.h"
#include "Util.h"

///
/// <summary>
/// Tracks the running extremum (maximum when constructed with trackMax=true, otherwise
/// minimum) of a sliding time window using a monotonic deque, so the current value can be
/// read in O(1) instead of rescanning every retained sample each frame. Candidates that can
/// never become the extremum again (because a more extreme value arrived after them) are
/// dropped as soon as a new value is pushed; the remainder are evicted once they fall
/// outside the caller-supplied window boundary.
/// </summary>
///
class MonotonicExtremaDeque
{
private:
   float* _values = nullptr;
   unsigned long* _ticks = nullptr;
   size_t _capacity = 0;
   size_t _head = 0;
   size_t _count = 0;
   const bool _trackMax;

public:
   explicit MonotonicExtremaDeque(bool trackMax) : _trackMax(trackMax) {}

   MonotonicExtremaDeque(const MonotonicExtremaDeque&) = delete;
   MonotonicExtremaDeque& operator=(const MonotonicExtremaDeque&) = delete;

   ~MonotonicExtremaDeque()
   {
      delete[] _values;
      delete[] _ticks;
   }

   /// <summary>
   /// Ensures the deque can hold at least the requested number of candidates, growing (and
   /// never shrinking) it as needed while preserving any candidates already queued.
   /// </summary>
   /// <param name="capacity">Minimum required capacity.</param>
   /// <returns>True if the deque has at least the requested capacity.</returns>
   bool ensureCapacity(size_t capacity)
   {
      if (capacity <= _capacity)
      {
         return true;
      }

      float* values = new (std::nothrow) float[capacity];
      unsigned long* ticks = new (std::nothrow) unsigned long[capacity];
      if (values == nullptr || ticks == nullptr)
      {
         delete[] values;
         delete[] ticks;
         return false;
      }

      for (size_t i = 0; i < _count; i++)
      {
         size_t oldIndex = (_head + i) % _capacity;
         values[i] = _values[oldIndex];
         ticks[i] = _ticks[oldIndex];
      }

      delete[] _values;
      delete[] _ticks;
      _values = values;
      _ticks = ticks;
      _capacity = capacity;
      _head = 0;
      return true;
   }

   /// <summary>
   /// Discards all queued candidates.
   /// </summary>
   void clear()
   {
      _head = 0;
      _count = 0;
   }

   /// <summary>
   /// Queues a new candidate, dropping any trailing (most recently queued) candidates that
   /// can never become the extremum again now that this value has arrived. Candidates must
   /// be pushed in non-decreasing tick order.
   /// </summary>
   /// <param name="value">Candidate value.</param>
   /// <param name="tick">Absolute timestamp (millis()) the value was sampled at.</param>
   void push(float value, unsigned long tick)
   {
      if (_capacity == 0)
      {
         return;
      }

      while (_count > 0)
      {
         size_t backIndex = (_head + _count - 1) % _capacity;
         bool backObsolete = _trackMax ? (_values[backIndex] <= value) : (_values[backIndex] >= value);
         if (!backObsolete)
         {
            break;
         }
         _count--;
      }

      if (_count >= _capacity)
      {
         return;
      }

      size_t tailIndex = (_head + _count) % _capacity;
      _values[tailIndex] = value;
      _ticks[tailIndex] = tick;
      _count++;
   }

   /// <summary>
   /// Evicts candidates older than the given window boundary.
   /// </summary>
   /// <param name="oldestSurvivingTick">Timestamp of the oldest sample still retained by the window.</param>
   void evictBefore(unsigned long oldestSurvivingTick)
   {
      while (_count > 0 && _ticks[_head] < oldestSurvivingTick)
      {
         _head = (_head + 1) % _capacity;
         _count--;
      }
   }

   /// <summary>
   /// Gets the current extremum.
   /// </summary>
   /// <param name="outValue">Receives the extremum value when one is queued.</param>
   /// <returns>True if a candidate was available.</returns>
   bool tryGetExtremum(float* outValue) const
   {
      if (_count == 0)
      {
         return false;
      }
      *outValue = _values[_head];
      return true;
   }
};

///
/// <summary>
/// Renders a scatter plot from a TimedValues buffer with time-windowed history.
/// Each pixel is displayed as either fully lit (green) or unlit (black).
/// Incremental rendering updates only changed pixels for efficiency.
/// </summary>
///
class TimedScatterPlot
{
private:
   ArduinoWithDisplay* _feather;
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

   bool* _pixelLit = nullptr;
   uint16_t* _previousPixels = nullptr;
   size_t _pixelCapacity = 0;

   unsigned long _lastRenderMicros = 0;
   unsigned long _lastComputeMicros = 0;
   unsigned long _lastDisplayMicros = 0;

   int16_t _lastShiftPixels = 0;
   size_t _lastAddedSamples = 0;
   bool _lastRebuiltPixels = false;
   unsigned long _lastAddWindowMs = 0;

   bool _hasPixelFrame = false;
   float _frameMinValue = NAN;
   float _frameMaxValue = NAN;
   int16_t _frameChartWidth = 0;
   int16_t _frameChartHeight = 0;
   unsigned long _lastFrameUpdateMs = 0;
   unsigned long _pendingShiftMs = 0;

   // Sliding-window min/max tracking, maintained incrementally across render() calls so
   // the axis range doesn't require rescanning every retained sample every frame.
   MonotonicExtremaDeque _minValueDeque{ false };
   MonotonicExtremaDeque _maxValueDeque{ true };
   bool _hasExtremaState = false;
   unsigned long _lastExtremaTickMs = 0;

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
         Util::setHaltReason("OOM allocating sample buffers in TimedScatterPlot");
         Util::reset();
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

      bool* pixelLit = new (std::nothrow) bool[capacity];
      uint16_t* previousPixels = new (std::nothrow) uint16_t[capacity];
      if (pixelLit == nullptr || previousPixels == nullptr)
      {
         Util::setHaltReason("OOM allocating pixel buffers in TimedScatterPlot");
         Util::reset();
         return false;
      }

      delete[] _pixelLit;
      delete[] _previousPixels;
      _pixelLit = pixelLit;
      _previousPixels = previousPixels;
      _pixelCapacity = capacity;

      memset(_pixelLit, 0, _pixelCapacity * sizeof(bool));
      for (size_t i = 0; i < _pixelCapacity; i++)
      {
         _previousPixels[i] = static_cast<uint16_t>(Color::BLACK);
      }

      return true;
   }

   /// <summary>
   /// Ensures the min/max extrema tracking deques have sufficient capacity to hold
   /// every retained sample as a candidate.
   /// </summary>
   /// <param name="capacity">Required capacity in elements.</param>
   /// <returns>True if allocation succeeded or was already sufficient; false on failure.</returns>
   bool _ensureExtremaBuffers(size_t capacity)
   {
      if (_minValueDeque.ensureCapacity(capacity) && _maxValueDeque.ensureCapacity(capacity))
      {
         return true;
      }

      Util::setHaltReason("OOM allocating extrema buffers in TimedScatterPlot");
      Util::reset();
      return false;
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
         _feather->display.writePixel(x, y, static_cast<uint16_t>(Color::BLACK));
         _previousPixels[i] = static_cast<uint16_t>(Color::BLACK);
      }
   }

   /// <summary>
   /// Shifts the pixel-lit array left by the specified pixel count.
   /// </summary>
   /// <param name="chartWidth">Width in pixels of the chart area.</param>
   /// <param name="chartHeight">Height in pixels of the chart area.</param>
   /// <param name="shiftPixels">Number of pixels to shift left.</param>
   void _shiftPixelsLeft(int16_t chartWidth, int16_t chartHeight, int16_t shiftPixels)
   {
      if (shiftPixels <= 0 || shiftPixels >= chartWidth)
      {
         return;
      }

      const size_t copyCount = static_cast<size_t>(chartWidth - shiftPixels);
      for (int16_t y = 0; y < chartHeight; y++)
      {
         bool* row = _pixelLit + static_cast<size_t>(y) * static_cast<size_t>(chartWidth);
         memmove(row, row + shiftPixels, copyCount * sizeof(bool));
         memset(row + copyCount, 0, static_cast<size_t>(shiftPixels) * sizeof(bool));
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
   /// Marks a single pixel location as lit.
   /// </summary>
   /// <param name="x">Pixel x-coordinate within the chart.</param>
   /// <param name="y">Pixel y-coordinate within the chart.</param>
   /// <param name="chartWidth">Width in pixels of the chart area.</param>
   /// <param name="chartHeight">Height in pixels of the chart area.</param>
   void _setPixelLit(int16_t x, int16_t y, int16_t chartWidth, int16_t chartHeight)
   {
      if (x < 0 || x >= chartWidth || y < 0 || y >= chartHeight)
      {
         return;
      }

      size_t pixelIndex = static_cast<size_t>(y) * static_cast<size_t>(chartWidth) + static_cast<size_t>(x);
      _pixelLit[pixelIndex] = true;
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
   /// Marks the pixels lit for a sample value at a given x-coordinate.
   /// Spans multiple pixels if the sensor resolution is coarser than pixel resolution.
   /// </summary>
   /// <param name="x">X-coordinate on the chart.</param>
   /// <param name="value">Sample value to plot.</param>
   /// <param name="plotMax">Maximum value in the plot range.</param>
   /// <param name="valuePerPixel">Y-axis scale (value units per pixel).</param>
   /// <param name="pixelsPerStep">Width of each sensor step in pixels.</param>
   /// <param name="chartWidth">Width in pixels of the chart area.</param>
   /// <param name="chartHeight">Height in pixels of the chart area.</param>
   void _setPixelsForValue(int16_t x, float value, float plotMax, float valuePerPixel, float pixelsPerStep, int16_t chartWidth, int16_t chartHeight)
   {
      if (valuePerPixel <= 0.0f)
      {
         return;
      }

      const float yFloat = (plotMax - value) / valuePerPixel;
      if (pixelsPerStep <= 1.0f)
      {
         int16_t y = static_cast<int16_t>(roundf(yFloat));
         _setPixelLit(x, y, chartWidth, chartHeight);
         return;
      }

      const float halfSpan = (pixelsPerStep - 1.0f) * 0.5f;
      int16_t yStart = static_cast<int16_t>(floorf(yFloat - halfSpan));
      int16_t yEnd = static_cast<int16_t>(ceilf(yFloat + halfSpan));

      for (int16_t y = yStart; y <= yEnd; y++)
      {
         _setPixelLit(x, y, chartWidth, chartHeight);
      }
   }

   /// <summary>
   /// Gets the display color for a pixel.
   /// </summary>
   /// <param name="index">Flat index into the pixel-lit/previous-pixel buffers.</param>
   /// <returns>Color::GREEN when the pixel is lit, otherwise Color::BLACK.</returns>
   uint16_t _pixelColor(size_t index) const
   {
      return _pixelLit[index] ? static_cast<uint16_t>(Color::GREEN) : static_cast<uint16_t>(Color::BLACK);
   }

public:
   /// <summary>
   /// Creates a timed scatter plot renderer using default formats.
   /// Default min/max format shows one decimal place right-aligned.
   /// Default sample range format shows one decimal place with 's' suffix, center-aligned.
   /// </summary>
   /// <param name="feather">Pointer to the display interface.</param>
   /// <param name="samples">Reference to the TimedValues buffer containing samples.</param>
   /// <param name="historyMs">Time window in milliseconds for the visible history.</param>
   /// <param name="minValueStep">Optional sensor resolution step size (0.0 for no alignment, default 0.0).</param>
   TimedScatterPlot(ArduinoWithDisplay* feather, TimedValues& samples, unsigned long historyMs, float minValueStep = 0.0f)
      : TimedScatterPlot(feather, samples, historyMs, Format("##.#", Format::Alignment::RIGHT), Format("##.#s", Format::Alignment::CENTER), minValueStep)
   {}

   /// <summary>
   /// Creates a timed scatter plot renderer with custom formats.
   /// </summary>
   TimedScatterPlot(ArduinoWithDisplay* feather, TimedValues& samples, unsigned long historyMs, const Format& minMaxFormat, const Format& sampleRangeFormat, float minValueStep = 0.0f)
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
      delete[] _pixelLit;
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

   bool lastRebuiltPixels() const
   {
      return _lastRebuiltPixels;
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

      _feather->setTextSize(2);

      const int16_t width = _feather->width();
      const int16_t height = _feather->height();
      const int16_t infoHeight = _feather->charH();
      const int16_t labelWidth = static_cast<int16_t>(_minMaxFormat.length() * _feather->charW());
      const int16_t chartLeft = labelWidth + _feather->charW();
      const int16_t chartTop = infoHeight + 2;
      const int16_t chartWidth = width - chartLeft - 1;
      const int16_t rangeLineHeight = _feather->charH() + 2;
      const int16_t chartHeight = height - chartTop - rangeLineHeight;

      if (chartWidth < 2 || chartHeight < 2)
      {
         const unsigned long displayStartMicros = micros();
         _feather->display.startWrite();
         _feather->println("Plot area too small", Color::LABEL);
         _feather->display.endWrite();
         _lastDisplayMicros = micros() - displayStartMicros;
         _lastRenderMicros = micros() - frameStartMicros;
         _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
         return;
      }

      if (!_ensureSampleBuffers(_samples.size()))
      {
         const unsigned long displayStartMicros = micros();
         _feather->display.startWrite();
         _feather->println("Memory unavailable", Color::LABEL);
         _feather->display.endWrite();
         _lastDisplayMicros = micros() - displayStartMicros;
         _lastRenderMicros = micros() - frameStartMicros;
         _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
         return;
      }

      if (!_ensureExtremaBuffers(_samples.size()))
      {
         const unsigned long displayStartMicros = micros();
         _feather->display.startWrite();
         _feather->println("Memory unavailable", Color::LABEL);
         _feather->display.endWrite();
         _lastDisplayMicros = micros() - displayStartMicros;
         _lastRenderMicros = micros() - frameStartMicros;
         _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
         return;
      }

      const size_t valueCount = _samples.snapshot(_sampleValues, _sampleAgesMs, _sampleCapacity);

      if (!_ensurePixelBuffers(static_cast<size_t>(chartWidth) * static_cast<size_t>(chartHeight)))
      {
         const unsigned long displayStartMicros = micros();
         _feather->display.startWrite();
         _feather->println("Memory unavailable", Color::LABEL);
         _feather->display.endWrite();
         _lastDisplayMicros = micros() - displayStartMicros;
         _lastRenderMicros = micros() - frameStartMicros;
         _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
         return;
      }

      _lastShiftPixels = 0;
      _lastAddedSamples = 0;
      _lastRebuiltPixels = false;
      _lastAddWindowMs = 0;

      float minValue = 0.0f;
      float maxValue = 0.0f;
      float axisMinValue = 0.0f;
      float axisMaxValue = 0.0f;
      float sampleRangeSeconds = NAN;

      if (valueCount > 0)
      {
         const unsigned long nowMs = millis();
         const unsigned long retentionMs = _samples.durationMs();

         // Maintain running min/max incrementally instead of rescanning every retained
         // sample each frame: push samples added since the last update (the newest-first
         // snapshot lets us stop as soon as we reach an already-seen sample), then evict
         // samples that have aged out of the retention window.
         const unsigned long elapsedExtremaMs = _hasExtremaState ? (nowMs - _lastExtremaTickMs) : (retentionMs + 1UL);

         size_t newExtremaCount = 0;
         while (newExtremaCount < valueCount && _sampleAgesMs[newExtremaCount] < elapsedExtremaMs)
         {
            newExtremaCount++;
         }

         for (size_t i = newExtremaCount; i-- > 0; )
         {
            const unsigned long tick = nowMs - _sampleAgesMs[i];
            _minValueDeque.push(_sampleValues[i], tick);
            _maxValueDeque.push(_sampleValues[i], tick);
         }

         const unsigned long oldestSurvivingTick = (nowMs >= retentionMs) ? (nowMs - retentionMs) : 0UL;
         _minValueDeque.evictBefore(oldestSurvivingTick);
         _maxValueDeque.evictBefore(oldestSurvivingTick);

         if (!_minValueDeque.tryGetExtremum(&minValue))
         {
            minValue = _sampleValues[0];
         }
         if (!_maxValueDeque.tryGetExtremum(&maxValue))
         {
            maxValue = _sampleValues[0];
         }

         _lastExtremaTickMs = nowMs;
         _hasExtremaState = true;

         minValue = _roundDownToAxisPrecision(minValue);
         maxValue = _roundUpToAxisPrecision(maxValue);

         const unsigned long msPerPixel = (_historyMs > 1UL)
            ? std::max<unsigned long>(1UL, (_historyMs - 1UL) / static_cast<unsigned long>(std::max<int16_t>(1, chartWidth - 1)))
            : 1UL;

         const bool sameChart = _hasPixelFrame && _frameChartWidth == chartWidth && _frameChartHeight == chartHeight;
         const float rangeEpsilon = _axisPrecisionEpsilon();
         const bool rangeChanged = _hasPixelFrame
            && (fabsf(minValue - _frameMinValue) > rangeEpsilon || fabsf(maxValue - _frameMaxValue) > rangeEpsilon);
         const bool stalePixels = _hasPixelFrame && ((nowMs - _lastFrameUpdateMs) > msPerPixel);
         const bool rebuildPixels = !_hasPixelFrame || !sameChart || rangeChanged || stalePixels;

         float plotMin = rebuildPixels ? minValue : _frameMinValue;
         float plotMax = rebuildPixels ? maxValue : _frameMaxValue;
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

         if (rebuildPixels)
         {
            _lastRebuiltPixels = true;
            _pendingShiftMs = 0;
            memset(_pixelLit, 0, _pixelCapacity * sizeof(bool));

            for (size_t i = 0; i < valueCount; i++)
            {
               float value = _sampleValues[i];
               if (alignToSensorStep)
               {
                  value = _alignToSensorStep(value);
               }

               value = _clampToPlotRange(value, plotMin, plotMax);
               int16_t x = _xForAgeMs(_sampleAgesMs[i], chartWidth);
               _setPixelsForValue(x, value, plotMax, valuePerPixel, pixelsPerStep, chartWidth, chartHeight);
            }
         }
         else
         {
            const unsigned long elapsedMs = nowMs - _lastFrameUpdateMs;
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
               _lastFrameUpdateMs = nowMs;
               _lastRenderMicros = micros() - frameStartMicros;
               _lastComputeMicros = _lastRenderMicros;
               _lastDisplayMicros = 0;
               return;
            }

            _pendingShiftMs -= static_cast<unsigned long>(shiftPixels) * msPerPixel;
            _lastShiftPixels = shiftPixels;
            _shiftPixelsLeft(chartWidth, chartHeight, shiftPixels);

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
               _setPixelsForValue(x, value, plotMax, valuePerPixel, pixelsPerStep, chartWidth, chartHeight);
            }
         }

         // Snapshot is newest-first, so the last entry already holds the maximum age.
         sampleRangeSeconds = static_cast<float>(_sampleAgesMs[valueCount - 1]) / 1000.0f;
         _hasPixelFrame = true;
         _frameMinValue = plotMin;
         _frameMaxValue = plotMax;
         _frameChartWidth = chartWidth;
         _frameChartHeight = chartHeight;
         _lastFrameUpdateMs = nowMs;
      }

      const unsigned long displayStartMicros = micros();

      _feather->display.startWrite();

      if (valueCount == 0)
      {
         _hasPixelFrame = false;
         _pendingShiftMs = 0;
         _hasExtremaState = false;
         _minValueDeque.clear();
         _maxValueDeque.clear();
         _clearChangedPixels(chartLeft, chartTop, chartWidth, chartHeight);
         _feather->display.endWrite();
         _lastDisplayMicros = micros() - displayStartMicros;
         _lastRenderMicros = micros() - frameStartMicros;
         _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
         return;
      }

      for (int16_t x = 0; x < chartWidth; x++)
      {
         int16_t y = 0;
         while (y < chartHeight)
         {
            size_t index = static_cast<size_t>(y) * static_cast<size_t>(chartWidth) + static_cast<size_t>(x);
            uint16_t newColor = _pixelColor(index);

            if (_previousPixels[index] == newColor)
            {
               y++;
               continue;
            }

            const int16_t runStart = y;
            _previousPixels[index] = newColor;
            y++;

            while (y < chartHeight)
            {
               size_t nextIndex = static_cast<size_t>(y) * static_cast<size_t>(chartWidth) + static_cast<size_t>(x);
               uint16_t nextColor = _pixelColor(nextIndex);
               if (nextColor != newColor || _previousPixels[nextIndex] == nextColor)
               {
                  break;
               }

               _previousPixels[nextIndex] = newColor;
               y++;
            }

            _feather->display.writeFastVLine(chartLeft + x, chartTop + runStart, y - runStart, newColor);
         }
      }

      _feather->fillRect(chartLeft, chartTop + chartHeight - 1, chartWidth, 1, Color::DARKGRAY);
      _feather->fillRect(chartLeft, chartTop, 1, chartHeight, Color::DARKGRAY);

      String axisMaxLabel = Util::toSignificantString(axisMaxValue, 3);
      String axisMinLabel = Util::toSignificantString(axisMinValue, 3);

      int16_t maxLabelX = labelWidth - static_cast<int16_t>(_feather->textWidth(axisMaxLabel.c_str()));
      _feather->setCursor(std::max<int16_t>(0, maxLabelX), chartTop);
      _feather->print(axisMaxLabel, Color::LABEL);

      int16_t minLabelX = labelWidth - static_cast<int16_t>(_feather->textWidth(axisMinLabel.c_str()));
      _feather->setCursor(std::max<int16_t>(0, minLabelX), chartTop + chartHeight - _feather->charH());
      _feather->print(axisMinLabel, Color::LABEL);

      if (isfinite(sampleRangeSeconds))
      {
         int16_t sampleRangeX = chartLeft + (chartWidth - static_cast<int16_t>(_sampleRangeFormat.length() * _feather->charW())) / 2;
         _feather->setCursor(sampleRangeX, chartTop + chartHeight + 1);
         _feather->print(sampleRangeSeconds, _sampleRangeFormat, Color::LABEL);
      }

      _feather->display.endWrite();

      _lastDisplayMicros = micros() - displayStartMicros;
      _lastRenderMicros = micros() - frameStartMicros;
      _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
   }
};
