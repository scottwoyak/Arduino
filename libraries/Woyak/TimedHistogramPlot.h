#pragma once

#include "TimedHistogram.h"
#include "Feather.h"
#include "TimedValues.h"

template<unsigned long (*TimeFunc)() = millis>
class TimedHistogramPlotBase
{
private:
   Feather& _feather;
   TimedHistogramBase<TimeFunc>& _histogram;
   TimedValuesBase<float, TimeFunc>& _samples;
   static constexpr unsigned long STALE_RENDER_GAP_MS = 250;

   int16_t* _previousBarHeights = nullptr;
   uint _previousRenderedBins = 0;
   float _previousMin = NAN;
   float _previousMax = NAN;
   float _previousSampleRangeSeconds = NAN;
   unsigned long _lastRenderMs = 0;
   Format _minMaxFormat = Format("##.#", Format::Alignment::RIGHT);
   Format _sampleRangeFormat = Format("##.#s", Format::Alignment::CENTER);

   bool _allocateRenderState()
   {
	  if (_previousBarHeights != nullptr)
	  {
		 return true;
	  }

	  const uint binCount = _histogram.getNumBinsX();
	  _previousBarHeights = new (std::nothrow) int16_t[binCount];
	  if (_previousBarHeights == nullptr)
	  {
		 return false;
	  }

	  for (uint i = 0; i < binCount; i++)
	  {
		 _previousBarHeights[i] = -1;
	  }

	  return true;
   }

   void _renderBars(const TimedHistogramSnapshot& snapshot)
   {
	  _feather.setTextSize(2);
	  _feather.display.startWrite();

	  const int16_t width = _feather.width();
	  const int16_t height = _feather.height();
	  const int16_t infoHeight = _feather.charH();
	  const int16_t labelHeight = _feather.charH() + 2;
	  const int16_t chartTop = infoHeight + 2;
	  const int16_t chartBottom = height - labelHeight;
	  const int16_t chartHeight = chartBottom - chartTop;
	  const int16_t chartWidth = width;

	  if (chartWidth < 2 || chartHeight < 2)
	  {
		 _feather.println("Plot area too small", Color::LABEL);
		 _feather.display.endWrite();
		 return;
	  }

	  uint renderedBins = snapshot.renderedBins;
	  if (renderedBins == 0)
	  {
		 renderedBins = 1;
	  }

	  if (_previousRenderedBins != renderedBins)
	  {
		 _feather.fillRect(0, chartTop, chartWidth, chartHeight, Color::BLACK);
		 for (uint i = 0; i < _histogram.getNumBinsX(); i++)
		 {
			_previousBarHeights[i] = 0;
		 }
		 _previousRenderedBins = renderedBins;
	  }

	  if (snapshot.valueCount == 0)
	  {
		 for (uint i = 0; i < renderedBins; i++)
		 {
			int16_t prevH = _previousBarHeights[i] < 0 ? 0 : _previousBarHeights[i];
			if (prevH > 0)
			{
			   int16_t x0 = static_cast<int16_t>((static_cast<int32_t>(i) * chartWidth) / renderedBins);
			   int16_t x1 = static_cast<int16_t>((static_cast<int32_t>(i + 1) * chartWidth) / renderedBins);
			   _feather.fillRect(x0, chartBottom - prevH, std::max<int16_t>(1, x1 - x0), prevH, Color::BLACK);
			   _previousBarHeights[i] = 0;
			}
		 }
		 _feather.display.endWrite();
		 return;
	  }

	  for (uint i = 0; i < renderedBins; i++)
	  {
		 int16_t x0 = static_cast<int16_t>((static_cast<int32_t>(i) * chartWidth) / renderedBins);
		 int16_t x1 = static_cast<int16_t>((static_cast<int32_t>(i + 1) * chartWidth) / renderedBins);
		 int16_t barWidth = std::max<int16_t>(1, x1 - x0);

		 int16_t barHeight = 0;
		 float normalized = _histogram.normalizedBinValue(i);
		 if (normalized > 0.0f)
		 {
			barHeight = static_cast<int16_t>(normalized * static_cast<float>(chartHeight - 1));
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
			   _feather.fillRect(x0, chartBottom - barHeight, barWidth, barHeight - prevH, Color::GREEN);
			}
			else
			{
			   _feather.fillRect(x0, chartBottom - prevH, barWidth, prevH - barHeight, Color::BLACK);
			}
			_previousBarHeights[i] = barHeight;
		 }
	  }

	  _feather.fillRect(0, chartBottom, chartWidth, 1, Color::DARKGRAY);

	  if (snapshot.minValue != _previousMin || snapshot.maxValue != _previousMax || snapshot.sampleRangeSeconds != _previousSampleRangeSeconds)
	  {
		 int16_t labelY = chartBottom + 2;
		 int16_t labelW = static_cast<int16_t>(_minMaxFormat.length() * _feather.charW());
		 int16_t rangeLabelW = static_cast<int16_t>(_sampleRangeFormat.length() * _feather.charW());
		 int16_t rangeX = (chartWidth - rangeLabelW) / 2;

		 _feather.setCursor(0, labelY);
		 _feather.print(snapshot.minValue, _minMaxFormat, Color::LABEL);

		 _feather.setCursor(chartWidth - labelW, labelY);
		 _feather.print(snapshot.maxValue, _minMaxFormat, Color::LABEL);

		 if (isfinite(snapshot.sampleRangeSeconds))
		 {
			_feather.setCursor(rangeX, labelY);
			_feather.print(snapshot.sampleRangeSeconds, _sampleRangeFormat, Color::LABEL);
		 }

		 _previousMin = snapshot.minValue;
		 _previousMax = snapshot.maxValue;
		 _previousSampleRangeSeconds = snapshot.sampleRangeSeconds;
	  }

	  _feather.display.endWrite();
   }

public:
   TimedHistogramPlotBase(Feather& feather, TimedHistogramBase<TimeFunc>& histogram, TimedValuesBase<float, TimeFunc>& samples)
	  : _feather(feather), _histogram(histogram), _samples(samples)
   {}

   ~TimedHistogramPlotBase()
   {
	  delete[] _previousBarHeights;
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

   void render()
   {
	  if (!_allocateRenderState())
	  {
		 _feather.setTextSize(2);
		 _feather.println("Memory unavailable", Color::LABEL);
		 return;
	  }

	  const unsigned long nowMs = TimeFunc();
	  const bool staleRender = (_lastRenderMs != 0) && ((nowMs - _lastRenderMs) > STALE_RENDER_GAP_MS);
	  if (staleRender)
	  {
		 _previousRenderedBins = 0;
		 _previousMin = NAN;
		 _previousMax = NAN;
		 _previousSampleRangeSeconds = NAN;
	  }

	  TimedHistogramSnapshot snapshot = _histogram.capture(_samples);
	  if (!_histogram.computeNormalizedBins(snapshot))
	  {
		 _feather.setTextSize(2);
		 _feather.println("Memory unavailable", Color::LABEL);
		 return;
	  }

	  _renderBars(snapshot);
	  _lastRenderMs = nowMs;
   }

   };

using TimedHistogramPlot = TimedHistogramPlotBase<millis>;
