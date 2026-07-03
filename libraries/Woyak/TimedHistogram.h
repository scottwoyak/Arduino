#pragma once

#include <new>
#include <algorithm>
#include "TimedStats.h"
#include "TimedValues.h"
#include "Feather.h"

/// <summary>
/// Tracks histogram data over a time window using binned statistics and can render
/// itself on a Feather display.
/// </summary>
/// <remarks>
/// This class divides a value range into equally-sized bins and maintains
/// time-windowed statistics for each bin using TimedStats.
/// Rendering state is lazily allocated on the first render() call.
/// </remarks>
template<unsigned long (*TimeFunc)() = millis>
class TimedHistogramBase
{
private:
   TimedStatsBase<TimeFunc>** _bins;
   uint _numBins;
   RangeF _range;
   Feather* _feather = nullptr;
   TimedValues* _samples = nullptr;

   // Rendering state (lazily allocated on first render() call)
   float* _binValues = nullptr;
   int16_t* _previousBarHeights = nullptr;
   float _previousMin = NAN;
   float _previousMax = NAN;
   float _previousSampleRangeSeconds = NAN;
   uint _previousRenderedBins = 0;
   float _minBinWidth = 0.0f;
   Format _minMaxFormat;
   Format _sampleRangeFormat;

   // Snapshot buffer for the dynamic render() overload
   float* _sampleValues = nullptr;
   unsigned long* _sampleAgesMs = nullptr;
   size_t _sampleCapacity = 0;

   unsigned long _lastRenderMicros = 0;
   unsigned long _lastComputeMicros = 0;
   unsigned long _lastDisplayMicros = 0;

   bool _allocateRenderBuffers()
   {
      if (_binValues != nullptr)
      {
         return true;
      }

      _binValues = new (std::nothrow) float[_numBins];
      _previousBarHeights = new (std::nothrow) int16_t[_numBins];

      if (_binValues == nullptr || _previousBarHeights == nullptr)
      {
         delete[] _binValues;
         _binValues = nullptr;
         delete[] _previousBarHeights;
         _previousBarHeights = nullptr;
         return false;
      }

      for (uint i = 0; i < _numBins; i++)
      {
         _previousBarHeights[i] = -1;
      }

      return true;
   }

   bool _ensureSampleBuffer(size_t capacity)
   {
      if (capacity <= _sampleCapacity)
      {
         return true;
      }

      float* nextValues = new (std::nothrow) float[capacity];
      unsigned long* nextAges = new (std::nothrow) unsigned long[capacity];
      if (nextValues == nullptr || nextAges == nullptr)
      {
         delete[] nextValues;
         delete[] nextAges;
         return false;
      }

      delete[] _sampleValues;
      delete[] _sampleAgesMs;
      _sampleValues = nextValues;
      _sampleAgesMs = nextAges;
      _sampleCapacity = capacity;
      return true;
   }

   uint _effectiveBinCount(float range) const
   {
      if (_numBins == 0)
      {
         return 0;
      }

      if (_minBinWidth <= 0.0f || range <= 0.0f)
      {
         return _numBins;
      }

      uint maxBinsByResolution = static_cast<uint>(floor(range / _minBinWidth + 0.0001f));
      if (maxBinsByResolution < 1)
      {
         maxBinsByResolution = 1;
      }

      return std::min(_numBins, maxBinsByResolution);
   }

   void _renderBars(Feather& feather, float minValue, float maxValue,
      float sampleRangeSeconds, size_t valueCount, uint renderedBins)
   {
      feather.setTextSize(2);
      feather.display.startWrite();

      const int16_t width = feather.width();
      const int16_t height = feather.height();
      const int16_t infoHeight = feather.charH();
      const int16_t labelHeight = feather.charH() + 2;
      const int16_t chartTop = infoHeight + 2;
      const int16_t chartBottom = height - labelHeight;
      const int16_t chartHeight = chartBottom - chartTop;
      const int16_t chartWidth = width;

      if (chartWidth < 2 || chartHeight < 2)
      {
         feather.println("Plot area too small", Color::LABEL);
         feather.display.endWrite();
         return;
      }

      if (renderedBins == 0)
      {
         renderedBins = 1;
      }

      if (_previousRenderedBins != renderedBins)
      {
         feather.fillRect(0, chartTop, chartWidth, chartHeight, Color::BLACK);
         for (uint i = 0; i < _numBins; i++)
         {
            _previousBarHeights[i] = 0;
         }
         _previousRenderedBins = renderedBins;
      }

      if (valueCount == 0)
      {
         for (uint i = 0; i < renderedBins; i++)
         {
            int16_t prevH = _previousBarHeights[i] < 0 ? 0 : _previousBarHeights[i];
            if (prevH > 0)
            {
               int16_t x0 = static_cast<int16_t>((static_cast<int32_t>(i) * chartWidth) / renderedBins);
               int16_t x1 = static_cast<int16_t>((static_cast<int32_t>(i + 1) * chartWidth) / renderedBins);
               feather.fillRect(x0, chartBottom - prevH, std::max<int16_t>(1, x1 - x0), prevH, Color::BLACK);
               _previousBarHeights[i] = 0;
            }
         }
         feather.display.endWrite();
         return;
      }

      for (uint i = 0; i < renderedBins; i++)
      {
         int16_t x0 = static_cast<int16_t>((static_cast<int32_t>(i) * chartWidth) / renderedBins);
         int16_t x1 = static_cast<int16_t>((static_cast<int32_t>(i + 1) * chartWidth) / renderedBins);
         int16_t barWidth = std::max<int16_t>(1, x1 - x0);

         int16_t barHeight = 0;
         if (_binValues[i] > 0.0f)
         {
            barHeight = static_cast<int16_t>(_binValues[i] * static_cast<float>(chartHeight - 1));
            if (barHeight < 1)
            {
               barHeight = 1;
            }
         }

         int16_t prevH = _previousBarHeights[i] < 0 ? 0 : _previousBarHeights[i];
         if (barHeight != prevH)
         {
            if (barHeight > prevH)
            {
               feather.fillRect(x0, chartBottom - barHeight, barWidth, barHeight - prevH, Color::GREEN);
            }
            else
            {
               feather.fillRect(x0, chartBottom - prevH, barWidth, prevH - barHeight, Color::BLACK);
            }
            _previousBarHeights[i] = barHeight;
         }
      }

      feather.fillRect(0, chartBottom, chartWidth, 1, Color::DARKGRAY);

      if (minValue != _previousMin || maxValue != _previousMax || sampleRangeSeconds != _previousSampleRangeSeconds)
      {
         int16_t labelY = chartBottom + 2;
         int16_t labelW = static_cast<int16_t>(_minMaxFormat.length() * feather.charW());
         int16_t rangeLabelW = static_cast<int16_t>(_sampleRangeFormat.length() * feather.charW());
         int16_t rangeX = (chartWidth - rangeLabelW) / 2;

         feather.setCursor(0, labelY);
         feather.print(minValue, _minMaxFormat, Color::LABEL);

         feather.setCursor(chartWidth - labelW, labelY);
         feather.print(maxValue, _minMaxFormat, Color::LABEL);

         if (isfinite(sampleRangeSeconds))
         {
            feather.setCursor(rangeX, labelY);
            feather.print(sampleRangeSeconds, _sampleRangeFormat, Color::LABEL);
         }

         _previousMin = minValue;
         _previousMax = maxValue;
         _previousSampleRangeSeconds = sampleRangeSeconds;
      }

      feather.display.endWrite();
   }

public:
   /// <summary>
   /// Creates a histogram for dynamic (sliding-scale) rendering only, with no fixed range.
   /// </summary>
   /// <param name="numBins">The number of bins.</param>
   /// <param name="ms">The time window duration in milliseconds (unused for dynamic rendering).</param>
   TimedHistogramBase(uint numBins, uint ms, float minBinWidth = 0.0f)
      : TimedHistogramBase(RangeF(0.0f, 1.0f), numBins, ms, minBinWidth, Format("##.#", Format::Alignment::RIGHT), Format("##.#s", Format::Alignment::CENTER))
   {}

   TimedHistogramBase(Feather& feather, TimedValues& samples, uint numBins, uint ms, float minBinWidth = 0.0f)
      : TimedHistogramBase(numBins, ms, minBinWidth)
   {
      _feather = &feather;
      _samples = &samples;
   }

   TimedHistogramBase(uint numBins, uint ms, float minBinWidth, const Format& minMaxFormat, const Format& sampleRangeFormat)
      : TimedHistogramBase(RangeF(0.0f, 1.0f), numBins, ms, minBinWidth, minMaxFormat, sampleRangeFormat)
   {}

   /// <summary>
   /// Creates a histogram with the specified range, number of bins, and time window.
   /// </summary>
   /// <param name="range">The min/max value range to histogram.</param>
   /// <param name="numBins">The number of bins to divide the range into.</param>
   /// <param name="ms">The time window duration in milliseconds.</param>
   TimedHistogramBase(RangeF range, uint numBins, uint ms, float minBinWidth = 0.0f)
      : TimedHistogramBase(range, numBins, ms, minBinWidth, Format("##.#", Format::Alignment::RIGHT), Format("##.#s", Format::Alignment::CENTER))
   {}

   TimedHistogramBase(Feather& feather, TimedValues& samples, RangeF range, uint numBins, uint ms, float minBinWidth = 0.0f)
      : TimedHistogramBase(range, numBins, ms, minBinWidth)
   {
      _feather = &feather;
      _samples = &samples;
   }

   TimedHistogramBase(RangeF range, uint numBins, uint ms, float minBinWidth, const Format& minMaxFormat, const Format& sampleRangeFormat)
      : _minMaxFormat(minMaxFormat), _sampleRangeFormat(sampleRangeFormat)
   {
      _range = range;
      _numBins = numBins;
      _minBinWidth = minBinWidth;
      _bins = new TimedStatsBase<TimeFunc>*[numBins];

      for (uint i = 0; i < numBins; i++)
      {
         _bins[i] = new TimedStatsBase<TimeFunc>(ms);
      }
   }

   /// <summary>
   /// Releases all bin and rendering resources.
   /// </summary>
   ~TimedHistogramBase()
   {
      for (uint i = 0; i < _numBins; i++)
      {
         delete _bins[i];
      }
      delete[] _bins;
      delete[] _binValues;
      delete[] _previousBarHeights;
      delete[] _sampleValues;
      delete[] _sampleAgesMs;
   }

   /// <summary>
   /// Gets the number of bins.
   /// </summary>
   /// <returns>The number of bins.</returns>
   uint getNumBinsX() const
   {
      return _numBins;
   }

   /// <summary>
   /// Gets the minimum value across all bins.
   /// </summary>
   /// <returns>The minimum value, or NaN if no values exist.</returns>
   float min() const
   {
      float value = __FLT_MAX__;
      for (uint i = 0; i < _numBins; i++)
      {
         value = std::min(value, _bins[i]->min());
      }

      return value;
   }

   /// <summary>
   /// Gets the maximum value across all bins.
   /// </summary>
   /// <returns>The maximum value, or NaN if no values exist.</returns>
   float max() const
   {
      float value = -__FLT_MAX__;
      for (uint i = 0; i < _numBins; i++)
      {
         value = std::max(value, _bins[i]->max());
      }

      return value;
   }

   /// <summary>
   /// Records a value in the appropriate bin.
   /// </summary>
   /// <param name="value">The value to record. NaN values are ignored.</param>
   void set(float value)
   {
      if (isnan(value))
      {
         return;
      }

      uint bin = floor((value - _range.min) / (_range.max - _range.min) * _numBins);

      bin = std::min(bin, _numBins - 1);
      bin = std::max(bin, 0u);

      _bins[bin]->set(value);
   }

   /// <summary>
   /// Gets normalized bin counts (0.0 to 1.0) based on the maximum count.
   /// </summary>
   /// <param name="values">Array to populate with normalized bin counts.</param>
   void get(float* values) const
   {
      float maxValue = 0;
      for (uint i = 0; i < _numBins; i++)
      {
         values[i] = _bins[i]->count();
         maxValue = std::max(maxValue, values[i]);
      }

      if (maxValue > 0)
      {
         for (uint i = 0; i < _numBins; i++)
         {
            values[i] /= maxValue;
         }
      }
   }

   /// <summary>
   /// Gets the range of bins containing data.
   /// </summary>
   /// <returns>A range with the first and last bin indices that have data, or the full range if empty.</returns>
   RangeU16 getCurrentBinsRange() const
   {
      int firstBin = -1;
      int lastBin = -1;

      for (uint i = 0; i < _numBins; i++)
      {
         if (firstBin == -1 && _bins[i]->count() > 0)
         {
            firstBin = i;
         }

         if (_bins[i]->count() > 0)
         {
            lastBin = i;
         }
      }

      if (firstBin == -1)
      {
         firstBin = 0;
         lastBin = _numBins - 1;
      }

      return RangeU16(firstBin, lastBin);
   }

   /// <summary>
   /// Gets the value range corresponding to bins with data.
   /// </summary>
   /// <returns>A range with the min/max values represented by the current bin range.</returns>
   RangeF getCurrentValuesRange() const
   {
      RangeU16 bins = getCurrentBinsRange();
      float span = (_range.max - _range.min) / _numBins;
      float min = _range.min + ((float)bins.min / _numBins) * (_range.max - _range.min);
      float max = _range.min + ((float)bins.max / _numBins) * (_range.max - _range.min) + span;
      return RangeF(min, max);
   }

   /// <summary>
   /// Gets the value range for a specific bin.
   /// </summary>
   /// <param name="bin">The bin index.</param>
   /// <returns>A range with the min/max values for the specified bin.</returns>
   RangeF getBinRange(uint16_t bin) const
   {
      float span = (_range.max - _range.min) / _numBins;
      float min = _range.min + ((float)bin / _numBins) * (_range.max - _range.min);
      float max = min + span;
      return RangeF(min, max);
   }

   /// <summary>
   /// Gets the total number of samples currently retained across all bins.
   /// </summary>
   size_t count() const
   {
      size_t total = 0;
      for (uint i = 0; i < _numBins; i++)
      {
         total += _bins[i]->count();
      }
      return total;
   }

   void setMinMaxFormat(const Format& format)
   {
      _minMaxFormat = format;
      _previousMin = NAN;
      _previousMax = NAN;
   }

   void setSampleRangeFormat(const Format& format)
   {
      _sampleRangeFormat = format;
      _previousSampleRangeSeconds = NAN;
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

   /// <summary>
   /// Resets rendering state, forcing a full redraw on the next render() call.
   /// </summary>
   void reset()
   {
      if (_previousBarHeights != nullptr)
      {
         for (uint i = 0; i < _numBins; i++)
         {
            _previousBarHeights[i] = -1;
         }
      }
      _previousMin = NAN;
      _previousMax = NAN;
      _previousSampleRangeSeconds = NAN;
      _previousRenderedBins = 0;
   }

   /// <summary>
   /// Draws the histogram using the bound display and timed sample source.
   /// </summary>
   void render()
   {
      if (_feather == nullptr || _samples == nullptr)
      {
         return;
      }

      render(*_feather, *_samples);
   }

   /// <summary>
   /// Draws the histogram using its fixed configured range.
   /// </summary>
   /// <param name="feather">Display driver.</param>
   void render(Feather& feather)
   {
      const unsigned long frameStartMicros = micros();

      if (!_allocateRenderBuffers())
      {
         const unsigned long displayStartMicros = micros();
         feather.setTextSize(2);
         feather.println("Memory unavailable", Color::LABEL);
         _lastDisplayMicros = micros() - displayStartMicros;
         _lastRenderMicros = micros() - frameStartMicros;
         _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
         return;
      }

      size_t totalCount = count();
      get(_binValues);
      const uint renderedBins = _effectiveBinCount(_range.max - _range.min);

      const unsigned long displayStartMicros = micros();
      _renderBars(feather, _range.min, _range.max, NAN, totalCount, renderedBins);
      _lastDisplayMicros = micros() - displayStartMicros;
      _lastRenderMicros = micros() - frameStartMicros;
      _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
   }

   /// <summary>
   /// Draws the histogram with a sliding scale derived from the current data window.
   /// </summary>
   /// <param name="feather">Display driver.</param>
   /// <param name="samples">Data source; the x-axis range tracks the current min/max.</param>
   void render(Feather& feather, TimedValues& samples)
   {
      const unsigned long frameStartMicros = micros();

      if (!_ensureSampleBuffer(samples.size()) || !_allocateRenderBuffers())
      {
         const unsigned long displayStartMicros = micros();
         feather.setTextSize(2);
         feather.println("Memory unavailable", Color::LABEL);
         _lastDisplayMicros = micros() - displayStartMicros;
         _lastRenderMicros = micros() - frameStartMicros;
         _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
         return;
      }

      size_t valueCount = samples.snapshot(_sampleValues, _sampleAgesMs, _sampleCapacity);
      float minValue = 0.0f;
      float maxValue = 0.0f;
      float sampleRangeSeconds = NAN;

      if (valueCount > 0)
      {
         minValue = _sampleValues[0];
         maxValue = minValue;
         for (size_t i = 1; i < valueCount; i++)
         {
            minValue = std::min(minValue, _sampleValues[i]);
            maxValue = std::max(maxValue, _sampleValues[i]);
         }

         float range = maxValue - minValue;
         if (range <= 0.0f)
         {
            range = 1.0f;
         }

         const uint renderedBins = _effectiveBinCount(range);

         unsigned long maxAgeMs = 0;
         for (size_t i = 0; i < valueCount; i++)
         {
            maxAgeMs = std::max(maxAgeMs, _sampleAgesMs[i]);
         }
         sampleRangeSeconds = static_cast<float>(maxAgeMs) / 1000.0f;

         for (uint i = 0; i < _numBins; i++)
         {
            _binValues[i] = 0.0f;
         }
         for (size_t i = 0; i < valueCount; i++)
         {
            int bin = static_cast<int>((_sampleValues[i] - minValue) / range * renderedBins);
            if (bin < 0) bin = 0;
            if (bin >= static_cast<int>(renderedBins)) bin = static_cast<int>(renderedBins) - 1;
            _binValues[bin] += 1.0f;
         }

         float maxBinValue = 0.0f;
         for (uint i = 0; i < renderedBins; i++)
         {
            maxBinValue = std::max(maxBinValue, _binValues[i]);
         }
         if (maxBinValue > 0.0f)
         {
            for (uint i = 0; i < renderedBins; i++)
            {
               _binValues[i] /= maxBinValue;
            }
         }

         const unsigned long displayStartMicros = micros();
         _renderBars(feather, minValue, maxValue, sampleRangeSeconds, valueCount, renderedBins);
         _lastDisplayMicros = micros() - displayStartMicros;
         _lastRenderMicros = micros() - frameStartMicros;
         _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
         return;
      }

      const unsigned long displayStartMicros = micros();
      _renderBars(feather, minValue, maxValue, sampleRangeSeconds, valueCount, _effectiveBinCount(0.0f));
      _lastDisplayMicros = micros() - displayStartMicros;
      _lastRenderMicros = micros() - frameStartMicros;
      _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
   }
};

using TimedHistogram = TimedHistogramBase<millis>;
