#pragma once

#include "Histogram.h"
#include "ArduinoWithDisplay.h"
#include "Util.h"

///
/// <summary>
/// Renders a static histogram on the Feather display with incremental bar updates.
/// This class tracks previous render state to enable efficient incremental redrawing
/// of histogram bars and labels.
/// </summary>
///
class HistogramPlot
{
private:
    ArduinoWithDisplay* _feather;
    const Histogram& _histogram;
    int16_t _sectionLeft;
    int16_t _sectionWidth;
    int16_t _sectionTop;
    int16_t _sectionHeight;
    Color _barColor;
    Color _axisLabelColor;
    uint8_t _labelSignificantDigits;

    int16_t* _previousBarHeights = nullptr;
    float _previousMin = NAN;
    float _previousMax = NAN;

    /// <summary>
    /// Allocates render state buffers on first use.
    /// </summary>
    /// <returns>True if allocation succeeded or was already allocated; false on allocation failure.</returns>
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

   /// <summary>
   /// Renders histogram bars and labels to the display with incremental updates.
   /// Only redraws bars that have changed since the last render.
   /// </summary>
   void _renderBars()
   {
      _feather->setTextSize(2);
      _feather->display.startWrite();

      int16_t chartTop = _sectionTop;
      int16_t labelHeight = _feather->charH() + 2;
      int16_t chartBottom = _sectionTop + _sectionHeight - labelHeight;
      int16_t chartHeight = chartBottom - chartTop;

      if (chartHeight <= 2 || _sectionWidth <= 8)
      {
        _feather->display.endWrite();
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
          int16_t x0 = _sectionLeft + static_cast<int16_t>((i * _sectionWidth) / _histogram.bins());
          int16_t x1 = _sectionLeft + static_cast<int16_t>(((i + 1) * _sectionWidth) / _histogram.bins());
          int16_t barWidth = std::max<int16_t>(1, x1 - x0);
          uint32_t binCount = _histogram.bin(i);
          int16_t barHeight = static_cast<int16_t>((binCount * static_cast<uint32_t>(chartHeight - 1)) / maxBin);
          if ((binCount > 0) && (barHeight < 1))
          {
            barHeight = 1;
          }

          int16_t prevH = _previousBarHeights[i] < 0 ? 0 : _previousBarHeights[i];
          if (barHeight != prevH)
          {
            if (barHeight > prevH)
            {
               _feather->fillRect(x0, chartBottom - barHeight, barWidth, barHeight - prevH, _barColor);
            }
            else
            {
               _feather->fillRect(x0, chartBottom - prevH, barWidth, prevH - barHeight, Color::BLACK);
            }
            _previousBarHeights[i] = barHeight;
          }
        }
      }

      _feather->fillRect(_sectionLeft, chartBottom - 1, _sectionWidth, 1, Color::DARKGRAY);

      if (_histogram.count() > 0 && (_histogram.min() != _previousMin || _histogram.max() != _previousMax))
      {
        String minLabel = Util::toSignificantString(_histogram.min(), _labelSignificantDigits);
        String maxLabel = Util::toSignificantString(_histogram.max(), _labelSignificantDigits);

        _feather->setCursor(_sectionLeft, chartBottom + 1);
        _feather->print(minLabel, _axisLabelColor);

        int16_t maxLabelX = _sectionLeft + _sectionWidth - static_cast<int16_t>(maxLabel.length() * _feather->charW());
        if (maxLabelX < _sectionLeft)
        {
          maxLabelX = _sectionLeft;
        }
        _feather->setCursor(maxLabelX, chartBottom + 1);
        _feather->print(maxLabel, _axisLabelColor);

        _previousMin = _histogram.min();
        _previousMax = _histogram.max();
      }

      _feather->display.endWrite();
   }

public:
    ///
    /// <summary>
    /// Constructs a histogram plot renderer with separate axis label color.
    /// </summary>
    /// <param name="feather">Pointer to the display interface.</param>
    /// <param name="histogram">Reference to the histogram to render.</param>
    /// <param name="sectionLeft">Left x-coordinate of the histogram panel.</param>
    /// <param name="sectionWidth">Width in pixels of the histogram panel.</param>
    /// <param name="sectionTop">Top y-coordinate of the histogram panel.</param>
    /// <param name="sectionHeight">Height in pixels of the histogram panel.</param>
    /// <param name="barColor">Color to use for histogram bars.</param>
    /// <param name="labelSignificantDigits">Number of significant digits for min/max labels.</param>
    /// <param name="axisLabelColor">Color to use for axis labels.</param>
    ///
    HistogramPlot(ArduinoWithDisplay* feather, const Histogram& histogram, int16_t sectionLeft, int16_t sectionWidth, int16_t sectionTop, int16_t sectionHeight, Color barColor, uint8_t labelSignificantDigits, Color axisLabelColor)
       : _feather(feather), _histogram(histogram), _sectionLeft(sectionLeft), _sectionWidth(sectionWidth), _sectionTop(sectionTop), _sectionHeight(sectionHeight), _barColor(barColor), _axisLabelColor(axisLabelColor), _labelSignificantDigits(labelSignificantDigits)
    {
    }

    ///
    /// <summary>
    /// Constructs a histogram plot renderer with axis labels matching bar color.
    /// </summary>
    /// <param name="feather">Pointer to the display interface.</param>
    /// <param name="histogram">Reference to the histogram to render.</param>
    /// <param name="sectionLeft">Left x-coordinate of the histogram panel.</param>
    /// <param name="sectionWidth">Width in pixels of the histogram panel.</param>
    /// <param name="sectionTop">Top y-coordinate of the histogram panel.</param>
    /// <param name="sectionHeight">Height in pixels of the histogram panel.</param>
    /// <param name="barColor">Color to use for histogram bars and axis labels.</param>
    /// <param name="labelSignificantDigits">Number of significant digits for min/max labels.</param>
    ///
    HistogramPlot(ArduinoWithDisplay* feather, const Histogram& histogram, int16_t sectionLeft, int16_t sectionWidth, int16_t sectionTop, int16_t sectionHeight, Color barColor, uint8_t labelSignificantDigits)
       : HistogramPlot(feather, histogram, sectionLeft, sectionWidth, sectionTop, sectionHeight, barColor, labelSignificantDigits, barColor)
    {
    }

    /// <summary>
    /// Destructor that releases allocated render state buffers.
    /// </summary>
    ~HistogramPlot()
    {
       delete[] _previousBarHeights;
    }

    /// <summary>
    /// Renders the histogram to the display with incremental updates.
    /// Call this method repeatedly to update the display as new data arrives.
    /// </summary>
    void render()
    {
       if (!_allocateRenderState())
       {
         _feather->setTextSize(2);
         _feather->println("Memory unavailable", Color::LABEL);
         return;
       }

       _renderBars();
    }
};
