#pragma once

#include "Histogram.h"
#include "ArduinoWithDisplay.h"
#include "Format.h"
#include "Util.h"

///
/// <summary>
/// Renders a static histogram on the Arduino display with incremental bar updates.
/// This class tracks previous render state to enable efficient incremental redrawing
/// of histogram bars and labels.
/// </summary>
///
class HistogramPlot
{
private:
   static constexpr int16_t Y_AXIS_LABEL_GAP = 2;

   ArduinoWithDisplay* _arduino;
   const Histogram& _histogram;
   Rect16 _rect;
   Color _barColor;
   Color _axisLabelColor;
   Format _minMaxFormat;
   bool _showYAxis;
   const char* _yAxisFormat;

   int16_t* _previousBarHeights = nullptr;
   float _previousMin = NAN;
   float _previousMax = NAN;
   uint32_t _previousMaxBin = 0;
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

      _previousBarHeights = new (std::nothrow) int16_t[_histogram.bins()];
      if (_previousBarHeights == nullptr)
      {
         Util::setHaltReason("OOM allocating bar heights in HistogramPlot");
         Util::reset();
         return false;
      }

      for (size_t i = 0; i < _histogram.bins(); i++)
      {
         _previousBarHeights[i] = -1;
      }

      return true;
   }

   ///
   /// <summary>
   /// Renders histogram bars and labels to the display with incremental updates.
   /// Only redraws bars that have changed since the last render.
   /// </summary>
   ///
   void _renderBars()
   {
      _arduino->setTextSize(2);
      _arduino->display.startWrite();

      int16_t chartTop = _rect.y;
      int16_t labelHeight = _arduino->charH() + 2;
      int16_t chartBottom = _rect.y + _rect.height - labelHeight;
      int16_t chartHeight = chartBottom - chartTop;

      uint32_t maxBinForWidth = _showYAxis ? std::max<uint32_t>(_previousMaxBin, 1) : 0;
      String topLabelForWidth;
      String bottomLabelForWidth;
      if (_showYAxis)
      {
         Format fmt(_yAxisFormat, Format::Alignment::RIGHT);
         topLabelForWidth = String(fmt.toString(maxBinForWidth).c_str());
         bottomLabelForWidth = String(fmt.toString(1.0).c_str());
         _yAxisWidth = static_cast<int16_t>(std::max(_arduino->textWidth(topLabelForWidth.c_str()), _arduino->textWidth(bottomLabelForWidth.c_str()))) + Y_AXIS_LABEL_GAP;
      }

      int16_t chartLeft = _rect.x + (_showYAxis ? _yAxisWidth : 0);
      int16_t chartWidth = _rect.width - (_showYAxis ? _yAxisWidth : 0);

      if (chartHeight <= 2 || chartWidth <= 8)
      {
         _arduino->display.endWrite();
         return;
      }

      uint32_t maxBin = 0;
      for (size_t i = 0; i < _histogram.bins(); i++)
      {
         maxBin = std::max(maxBin, _histogram.bin(i));
      }

      if (maxBin > 0)
      {
         for (size_t i = 0; i < _histogram.bins(); i++)
         {
            int16_t x0 = chartLeft + static_cast<int16_t>((i * chartWidth) / _histogram.bins());
            int16_t x1 = chartLeft + static_cast<int16_t>(((i + 1) * chartWidth) / _histogram.bins());
            int16_t barWidth = std::max<int16_t>(1, x1 - x0);
            uint32_t binCount = _histogram.bin(i);
            int16_t barHeight = static_cast<int16_t>((binCount * (chartHeight - 1)) / maxBin);
            if ((binCount > 0) && (barHeight < 1))
            {
               barHeight = 1;
            }

            int16_t prevH = _previousBarHeights[i] < 0 ? 0 : _previousBarHeights[i];
            if (barHeight != prevH)
            {
               if (barHeight > prevH)
               {
                  _arduino->fillRect(x0, chartBottom - barHeight, barWidth, barHeight - prevH, _barColor);
               }
               else
               {
                  _arduino->fillRect(x0, chartBottom - prevH, barWidth, prevH - barHeight, Color::BLACK);
               }
               _previousBarHeights[i] = barHeight;
            }
         }
      }

      _arduino->fillRect(chartLeft, chartBottom - 1, chartWidth, 1, Color::DARKGRAY);

      if (_showYAxis && !_yAxisDrawn)
      {
         _arduino->fillRect(chartLeft, chartTop, 1, chartHeight, Color::GRAY);
         _yAxisDrawn = true;
      }

      if (_showYAxis && (maxBin != _previousMaxBin))
      {
         Format fmt(_yAxisFormat, Format::Alignment::RIGHT);
         String topLabel = String(fmt.toString(maxBin).c_str());
         String bottomLabel = String(fmt.toString(1.0).c_str());

         int16_t topLabelX = chartLeft - Y_AXIS_LABEL_GAP - static_cast<int16_t>(_arduino->textWidth(topLabel.c_str()));
         _arduino->setCursor(topLabelX, chartTop);
         _arduino->print(topLabel, _axisLabelColor);

         int16_t bottomLabelX = chartLeft - Y_AXIS_LABEL_GAP - static_cast<int16_t>(_arduino->textWidth(bottomLabel.c_str()));
         _arduino->setCursor(bottomLabelX, chartBottom - _arduino->charH());
         _arduino->print(bottomLabel, _axisLabelColor);

         _previousMaxBin = maxBin;
      }

      if (_histogram.count() > 0 && (_histogram.min() != _previousMin || _histogram.max() != _previousMax))
      {
         String minLabel = String(_minMaxFormat.toString(_histogram.min()).c_str());
         String maxLabel = String(_minMaxFormat.toString(_histogram.max()).c_str());

         _arduino->setCursor(chartLeft, chartBottom + 1);
         _arduino->print(minLabel, _axisLabelColor);

         int16_t maxLabelX = chartLeft + chartWidth - static_cast<int16_t>(maxLabel.length() * _arduino->charW());
         if (maxLabelX < chartLeft)
         {
            maxLabelX = chartLeft;
         }
         _arduino->setCursor(maxLabelX, chartBottom + 1);
         _arduino->print(maxLabel, _axisLabelColor);

         _previousMin = _histogram.min();
         _previousMax = _histogram.max();
      }

      _arduino->display.endWrite();
   }

public:
   ///
   /// <summary>
   /// Constructs a histogram plot renderer with separate axis label color.
   /// </summary>
   /// <param name="arduino">Pointer to the display interface.</param>
   /// <param name="histogram">Reference to the histogram to render.</param>
   /// <param name="rect">Bounding rectangle of the histogram panel.</param>
   /// <param name="barColor">Color to use for histogram bars.</param>
   /// <param name="minMaxFormat">Format to use for min/max value labels.</param>
   /// <param name="axisLabelColor">Color to use for axis labels.</param>
   /// <param name="yAxisFormat">If not nullptr, reserves a left-side Y-axis column sized to
   /// fit labels formatted with this pattern (e.g. "####"), showing the max bin count at
   /// the top and "1" at the bottom (with a vertical axis line); pass nullptr (the
   /// default) for no Y-axis. To align another chart's x-axis (e.g. a ScatterPlot's) with
   /// this histogram, give both the same format string length so their reserved label
   /// columns end up the same width.</param>
   ///
   HistogramPlot(ArduinoWithDisplay* arduino, const Histogram& histogram, Rect16 rect, Color barColor, const Format& minMaxFormat, Color axisLabelColor, const char* yAxisFormat = nullptr)
      : _arduino(arduino), _histogram(histogram), _rect(rect), _barColor(barColor), _axisLabelColor(axisLabelColor), _minMaxFormat(minMaxFormat), _showYAxis(yAxisFormat != nullptr), _yAxisFormat(yAxisFormat)
   {
   }

   ///
   /// <summary>
   /// Constructs a histogram plot renderer with axis labels matching bar color. Bar color
   /// defaults to Color::GREEN and the min/max label format defaults to "##.##" if not specified.
   /// </summary>
   /// <param name="arduino">Pointer to the display interface.</param>
   /// <param name="histogram">Reference to the histogram to render.</param>
   /// <param name="rect">Bounding rectangle of the histogram panel.</param>
   /// <param name="barColor">Color to use for histogram bars and axis labels (default Color::GREEN).</param>
   /// <param name="minMaxFormat">Format to use for min/max value labels (default "##.##").</param>
   /// <param name="yAxisFormat">If not nullptr, reserves a left-side Y-axis column sized to
   /// fit labels formatted with this pattern (e.g. "####"), showing the max bin count at
   /// the top and "1" at the bottom (with a vertical axis line); pass nullptr (the
   /// default) for no Y-axis. To align another chart's x-axis (e.g. a ScatterPlot's) with
   /// this histogram, give both the same format string length so their reserved label
   /// columns end up the same width.</param>
   ///
   HistogramPlot(ArduinoWithDisplay* arduino, const Histogram& histogram, Rect16 rect, Color barColor = Color::GREEN, const Format& minMaxFormat = Format("##.##"), const char* yAxisFormat = nullptr)
      : HistogramPlot(arduino, histogram, rect, barColor, minMaxFormat, barColor, yAxisFormat)
   {
   }

   ///
   /// <summary>
   /// Destructor that releases allocated render state buffers.
   /// </summary>
   ///
   ~HistogramPlot()
   {
      delete[] _previousBarHeights;
   }

   ///
   /// <summary>
   /// Renders the histogram to the display with incremental updates.
   /// Call this method repeatedly to update the display as new data arrives.
   /// </summary>
   ///
   void render()
   {
      if (!_allocateRenderState())
      {
         _arduino->setTextSize(2);
         _arduino->println("Memory unavailable", Color::LABEL);
         return;
      }

      _renderBars();
   }
};
