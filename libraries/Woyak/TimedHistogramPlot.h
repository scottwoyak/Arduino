#pragma once

#include "TimedHistogram.h"
#include "ArduinoWithDisplay.h"
#include "TimedValues.h"
#include "Util.h"

///
/// <summary>
/// Renders a live-updating histogram of timed sample values on the Arduino display,
/// drawing bars within a fixed screen rectangle with incremental bar and label updates.
/// </summary>
///
template<unsigned long (*TimeFunc)() = millis>
class TimedHistogramPlotBase
{
private:
   static constexpr int16_t Y_AXIS_LABEL_GAP = 2;

   ArduinoWithDisplay* _feather;
   TimedHistogramBase<TimeFunc>& _histogram;
   TimedValuesBase<float, TimeFunc>& _samples;
   static constexpr unsigned long STALE_RENDER_GAP_MS = 250;

   int16_t* _previousBarHeights = nullptr;
   uint16_t _previousRenderedBins = 0;
   float _previousMin = NAN;
   float _previousMax = NAN;
   float _previousSampleRangeSeconds = NAN;
   float _previousMaxBinCount = -1.0f;
   unsigned long _lastRenderMs = 0;
   Format _sampleRangeFormat = Format("##.#s", Format::Alignment::CENTER);
   Format _minMaxFormat = Format("##.##");
   Rect16 _rect;
   bool _showYAxis;
   const char* _yAxisFormat;
   bool _yAxisDrawn = false;
   int16_t _yAxisWidth = 0;

   ///
   /// <summary>
   /// Allocates render state buffers on first use.
   /// </summary>
   /// <returns>True if allocation succeeded or was already allocated; false on allocation failure.</returns>
   ///
   bool _allocateRenderState()
   {
     if (_previousBarHeights != nullptr)
     {
       return true;
     }

     const uint16_t binCount = _histogram.getNumBinsX();
     _previousBarHeights = new (std::nothrow) int16_t[binCount];
     if (_previousBarHeights == nullptr)
     {
       Util::setHaltReason("OOM allocating bar heights in TimedHistogramPlot");
       Util::reset();
       return false;
     }

     for (uint16_t i = 0; i < binCount; i++)
     {
       _previousBarHeights[i] = -1;
     }

     return true;
   }

   ///
   /// <summary>
   /// Renders histogram bars and min/max/sample-range labels to the display, incrementally
   /// redrawing only the bars and labels that have changed since the last render.
   /// </summary>
   /// <param name="snapshot">Captured histogram data to render.</param>
   ///
   void _renderBars(const TimedHistogramSnapshot& snapshot)
   {
     _feather->setTextSize(2);
     _feather->display.startWrite();

     const int16_t labelHeight = _feather->charH() + 2;
     const int16_t chartTop = _rect.y;
     const int16_t chartBottom = _rect.y + _rect.height - labelHeight;
     const int16_t chartHeight = chartBottom - chartTop;

     uint32_t maxBinForWidth = _showYAxis ? std::max<uint32_t>(static_cast<uint32_t>(_previousMaxBinCount), 1) : 0;
     String topLabelForWidth;
     String bottomLabelForWidth;
     if (_showYAxis)
     {
       Format fmt(_yAxisFormat, Format::Alignment::RIGHT);
       topLabelForWidth = String(fmt.toString(maxBinForWidth).c_str());
       bottomLabelForWidth = String(fmt.toString(1.0).c_str());
       _yAxisWidth = static_cast<int16_t>(std::max(_feather->textWidth(topLabelForWidth.c_str()), _feather->textWidth(bottomLabelForWidth.c_str()))) + Y_AXIS_LABEL_GAP;
     }

     const int16_t chartLeft = _rect.x + (_showYAxis ? _yAxisWidth : 0);
     const int16_t chartWidth = _rect.width - (_showYAxis ? _yAxisWidth : 0);

     if (chartWidth < 2 || chartHeight < 2)
     {
       _feather->println("Plot area too small", Color::LABEL);
       _feather->display.endWrite();
       return;
     }

     uint16_t renderedBins = snapshot.renderedBins;
     if (renderedBins == 0)
     {
       renderedBins = 1;
     }

     if (_previousRenderedBins != renderedBins)
     {
       _feather->fillRect(chartLeft, chartTop, chartWidth, chartHeight, Color::BLACK);
       for (uint16_t i = 0; i < _histogram.getNumBinsX(); i++)
       {
         _previousBarHeights[i] = 0;
       }
       _previousRenderedBins = renderedBins;
     }

     if (snapshot.valueCount == 0)
     {
       for (uint16_t i = 0; i < renderedBins; i++)
       {
         int16_t prevH = _previousBarHeights[i] < 0 ? 0 : _previousBarHeights[i];
         if (prevH > 0)
         {
            int16_t x0 = static_cast<int16_t>((i * chartWidth) / renderedBins);
            int16_t x1 = static_cast<int16_t>(((i + 1) * chartWidth) / renderedBins);
            _feather->fillRect(chartLeft + x0, chartBottom - prevH, std::max<int16_t>(1, x1 - x0), prevH, Color::BLACK);
            _previousBarHeights[i] = 0;
         }
       }
       _feather->display.endWrite();
       return;
     }

     for (uint16_t i = 0; i < renderedBins; i++)
     {
       int16_t x0 = static_cast<int16_t>((i * chartWidth) / renderedBins);
       int16_t x1 = static_cast<int16_t>(((i + 1) * chartWidth) / renderedBins);
       int16_t barWidth = std::max<int16_t>(1, x1 - x0);

       int16_t barHeight = 0;
       float normalized = _histogram.normalizedBinValue(i);
       if (normalized > 0.0f)
       {
         barHeight = static_cast<int16_t>(normalized * (chartHeight - 1));
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
            _feather->fillRect(chartLeft + x0, chartBottom - barHeight, barWidth, barHeight - prevH, Color::GREEN);
         }
         else
         {
            _feather->fillRect(chartLeft + x0, chartBottom - prevH, barWidth, prevH - barHeight, Color::BLACK);
         }
         _previousBarHeights[i] = barHeight;
       }
     }

     _feather->fillRect(chartLeft, chartBottom, chartWidth, 1, Color::DARKGRAY);

     if (_showYAxis && !_yAxisDrawn)
     {
       _feather->fillRect(chartLeft, chartTop, 1, chartHeight, Color::GRAY);
       _yAxisDrawn = true;
     }

     if (_showYAxis && (snapshot.maxBinCount != _previousMaxBinCount))
     {
       Format fmt(_yAxisFormat, Format::Alignment::RIGHT);
       String topLabel = String(fmt.toString(snapshot.maxBinCount).c_str());
       String bottomLabel = String(fmt.toString(1.0).c_str());

       int16_t topLabelX = chartLeft - Y_AXIS_LABEL_GAP - static_cast<int16_t>(_feather->textWidth(topLabel.c_str()));
       _feather->setCursor(topLabelX, chartTop);
       _feather->print(topLabel, Color::LABEL);

       int16_t bottomLabelX = chartLeft - Y_AXIS_LABEL_GAP - static_cast<int16_t>(_feather->textWidth(bottomLabel.c_str()));
       _feather->setCursor(bottomLabelX, chartBottom - _feather->charH());
       _feather->print(bottomLabel, Color::LABEL);

       _previousMaxBinCount = snapshot.maxBinCount;
     }

     if (snapshot.minValue != _previousMin || snapshot.maxValue != _previousMax || snapshot.sampleRangeSeconds != _previousSampleRangeSeconds)
     {
       int16_t labelY = chartBottom + 2;
       int16_t rangeLabelW = static_cast<int16_t>(_sampleRangeFormat.length() * _feather->charW());
       int16_t rangeX = chartLeft + (chartWidth - rangeLabelW) / 2;

       String minLabel = String(_minMaxFormat.toString(snapshot.minValue).c_str());
       String maxLabel = String(_minMaxFormat.toString(snapshot.maxValue).c_str());

       _feather->setCursor(chartLeft, labelY);
       _feather->print(minLabel, Color::LABEL);

       int16_t maxLabelX = chartLeft + chartWidth - static_cast<int16_t>(maxLabel.length() * _feather->charW());
       _feather->setCursor(maxLabelX, labelY);
       _feather->print(maxLabel, Color::LABEL);

       if (isfinite(snapshot.sampleRangeSeconds))
       {
         _feather->setCursor(rangeX, labelY);
         _feather->print(snapshot.sampleRangeSeconds, _sampleRangeFormat, Color::GRAY);
       }

       _previousMin = snapshot.minValue;
       _previousMax = snapshot.maxValue;
       _previousSampleRangeSeconds = snapshot.sampleRangeSeconds;
     }

     _feather->display.endWrite();
   }

public:
   ///
   /// <summary>
   /// Constructs a timed histogram plot renderer that draws within the given fixed screen
   /// rectangle.
   /// </summary>
   /// <param name="feather">Pointer to the display interface.</param>
   /// <param name="histogram">Reference to the histogram to render.</param>
   /// <param name="samples">Reference to the timed values sampled into the histogram.</param>
   /// <param name="rect">Fixed screen rectangle this plot draws within; unchanged for the plot's lifetime.</param>
   /// <param name="yAxisFormat">If not nullptr, reserves a left-side Y-axis column sized to
   /// fit labels formatted with this pattern (e.g. "####"), showing the max bin count at
   /// the top and "1" at the bottom (with a vertical axis line); pass nullptr (the
   /// default) for no Y-axis.</param>
   ///
   TimedHistogramPlotBase(ArduinoWithDisplay* feather, TimedHistogramBase<TimeFunc>& histogram, TimedValuesBase<float, TimeFunc>& samples, Rect16 rect, const char* yAxisFormat = nullptr)
     : _feather(feather), _histogram(histogram), _samples(samples), _rect(rect), _showYAxis(yAxisFormat != nullptr), _yAxisFormat(yAxisFormat)
   {}

   ///
   /// <summary>
   /// Destructor that releases allocated render state buffers.
   /// </summary>
   ///
   ~TimedHistogramPlotBase()
   {
     delete[] _previousBarHeights;
   }

   ///
   /// <summary>
   /// Sets the format used to render the sample range (in seconds) label and forces it to
   /// be redrawn on the next render.
   /// </summary>
   /// <param name="format">Format to apply to the sample range label.</param>
   ///
   void setSampleRangeFormat(const Format& format)
   {
     _sampleRangeFormat = format;
     _previousSampleRangeSeconds = NAN;
   }

   ///
   /// <summary>
   /// Sets the format used to render the min/max sample value labels and forces them to
   /// be redrawn on the next render.
   /// </summary>
   /// <param name="format">Format to apply to the min/max value labels.</param>
   ///
   void setMinMaxFormat(const Format& format)
   {
     _minMaxFormat = format;
     _previousMin = NAN;
     _previousMax = NAN;
   }

   ///
   /// <summary>
   /// Renders the timed histogram to the display with incremental updates.
   /// Call this method repeatedly to update the display as new data arrives.
   /// </summary>
   ///
   void render()
   {
     if (!_allocateRenderState())
     {
       _feather->setTextSize(2);
       _feather->println("Memory unavailable", Color::LABEL);
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
         _previousMaxBinCount = -1.0f;
       }

     TimedHistogramSnapshot snapshot = _histogram.capture(_samples);
     if (!_histogram.computeNormalizedBins(snapshot))
     {
       _feather->setTextSize(2);
       _feather->println("Memory unavailable", Color::LABEL);
       return;
     }

     _renderBars(snapshot);
     _lastRenderMs = nowMs;
   }
};

///
/// <summary>
/// Convenience alias for <see cref="TimedHistogramPlotBase"/> using the standard
/// Arduino millis() time source.
/// </summary>
///
using TimedHistogramPlot = TimedHistogramPlotBase<millis>;
