#pragma once

#include <Arduino.h>
#include <algorithm>
#include <math.h>

#include "Feather.h"
#include "Histogram.h"
#include "SensorCaptureStats.h"
#include "SerialX.h"

/// <summary>
/// Provides reusable serial/display formatting helpers for sensor capture output.
/// </summary>
class SensorCaptureOutput
{
public:
   /// <summary>
   /// Formats a floating-point value to a target number of significant digits.
   /// </summary>
   /// <param name="value">Value to format.</param>
   /// <param name="significantDigits">Number of significant digits to preserve.</param>
   /// <returns>Formatted numeric text, or "n/a" for non-finite input.</returns>
   static String toSignificantString(float value, uint8_t significantDigits)
   {
      if (!isfinite(value))
      {
         return "n/a";
      }

      if (value == 0.0f)
      {
         return "0";
      }

      float absValue = fabsf(value);
      int exponent = static_cast<int>(floorf(log10f(absValue)));
      int decimals = static_cast<int>(significantDigits) - 1 - exponent;
      if (decimals <= 0)
      {
         long truncated = static_cast<long>(value);
         return String(truncated);
      }

      return String(value, static_cast<unsigned int>(decimals));
   }

   /// <summary>
   /// Prints one aligned statistics row to Serial.
   /// </summary>
   /// <param name="label">Metric label.</param>
   /// <param name="average">Average value.</param>
   /// <param name="stdDev">Standard deviation value.</param>
   /// <param name="minValue">Minimum value.</param>
   /// <param name="maxValue">Maximum value.</param>
   /// <param name="decimals">Decimal precision for numeric values.</param>
   static void printStatsRow(const char* label, float average, float stdDev, float minValue, float maxValue, uint8_t decimals)
   {
      float range = (isfinite(minValue) && isfinite(maxValue)) ? (maxValue - minValue) : NAN;

      SerialX::print(label, 20);
      SerialX::print(average, decimals, 12);
      SerialX::print(stdDev, decimals, 12);
      SerialX::print(minValue, decimals, 12);
      SerialX::print(maxValue, decimals, 12);
      SerialX::println(range, decimals, 12);
   }

   /// <summary>
   /// Computes and prints the standard capture summary to Serial.
   /// </summary>
   /// <param name="capture">Capture source used to compute summary statistics.</param>
   /// <param name="captureDurationMs">Configured capture duration in milliseconds.</param>
   /// <param name="decimals">Decimal precision for printed floating-point values.</param>
   static void printCaptureSummary(const SensorCapture& capture, unsigned long captureDurationMs, uint8_t decimals = 3)
   {
      SensorCaptureStats analysis(capture);
      Stats basicStats = analysis.computeBasicStats();

      float valueAvg = basicStats.get();
      float valueStdDev = basicStats.stdDev();
      float valueMin = basicStats.min();
      float valueMax = basicStats.max();
      float valueRange = analysis.computeRange(valueMin, valueMax);

      Serial.println();
      Serial.println("Capture Summary");
      SerialX::print("Capture ms", 20);
      SerialX::println(captureDurationMs, 20);
      SerialX::print("Samples", 20);
      SerialX::println(capture.count(), 20);
      SerialX::print("Sensor Avg", 20);
      SerialX::println(valueAvg, decimals, 20);
      SerialX::print("Sensor StdDev", 20);
      SerialX::println(valueStdDev, decimals, 20);
      SerialX::print("Sensor Min", 20);
      SerialX::println(valueMin, decimals, 20);
      SerialX::print("Sensor Max", 20);
      SerialX::println(valueMax, decimals, 20);
      SerialX::print("Sensor Range", 20);
      SerialX::println(valueRange, decimals, 20);
      Serial.println();
   }

   /// <summary>
   /// Builds and prints histogram bin ranges and counts to Serial.
   /// </summary>
   /// <typeparam name="BIN_COUNT">Histogram bin count.</typeparam>
   /// <param name="title">Histogram section title.</param>
   /// <param name="capture">Capture source used to build histogram bins.</param>
   /// <param name="decimals">Decimal precision for bin edges.</param>
   template<size_t BIN_COUNT>
   static void printHistogramBinsForCount(const char* title, const SensorCapture& capture, uint8_t decimals)
   {
      Histogram<BIN_COUNT> histogram;
      SensorCaptureStats analysis(capture);
      analysis.buildHistogram(histogram);

      SerialX::printHistogramBins(title, histogram, decimals);
   }

   /// <summary>
   /// Builds and prints histogram bin ranges and counts to Serial.
   /// </summary>
   /// <param name="title">Histogram section title.</param>
   /// <param name="capture">Capture source used to build histogram bins.</param>
   /// <param name="binCount">Histogram bin count.</param>
   /// <param name="decimals">Decimal precision for bin edges.</param>
   static void printHistogramBins(const char* title, const SensorCapture& capture, size_t binCount, uint8_t decimals)
   {
      switch (binCount)
      {
      case 5: printHistogramBinsForCount<5>(title, capture, decimals); break;
      case 10: printHistogramBinsForCount<10>(title, capture, decimals); break;
      case 20: printHistogramBinsForCount<20>(title, capture, decimals); break;
      case 30: printHistogramBinsForCount<30>(title, capture, decimals); break;
      case 40: printHistogramBinsForCount<40>(title, capture, decimals); break;
      case 50: printHistogramBinsForCount<50>(title, capture, decimals); break;
      default:
         Serial.println(title);
         Serial.print("Unsupported bin count: ");
         Serial.println(binCount);
         Serial.println();
         break;
      }
   }

   /// <summary>
   /// Draws a histogram panel on the Feather display.
   /// </summary>
   /// <typeparam name="BIN_COUNT">Histogram bin count.</typeparam>
   /// <param name="feather">Display driver.</param>
   /// <param name="title">Panel title.</param>
   /// <param name="histogram">Histogram to render.</param>
   /// <param name="sectionLeft">Left X coordinate of the panel.</param>
   /// <param name="sectionWidth">Panel width in pixels.</param>
   /// <param name="sectionTop">Top Y coordinate of the panel.</param>
   /// <param name="sectionHeight">Panel height in pixels.</param>
   /// <param name="barColor">Color used for bars and labels.</param>
   /// <param name="labelSignificantDigits">Significant digits used for min/max labels.</param>
   template<size_t BIN_COUNT>
   static void drawHistogramOnFeather(
      Feather& feather,
      const char* title,
      const Histogram<BIN_COUNT>& histogram,
      int16_t sectionLeft,
      int16_t sectionWidth,
      int16_t sectionTop,
      int16_t sectionHeight,
      Color barColor,
      uint8_t labelSignificantDigits)
   {
      if ((sectionHeight <= 16) || (sectionWidth <= 8))
      {
         return;
      }

      feather.setTextSize(2);
      feather.setCursor(sectionLeft, sectionTop);
      feather.println(title, Color::LABEL);

      int16_t chartTop = feather.getCursorY() + 1;
      int16_t labelHeight = feather.charH() + 2;
      int16_t sectionBottom = sectionTop + sectionHeight;
      int16_t chartBottom = sectionBottom - labelHeight;
      int16_t chartHeight = chartBottom - chartTop;
      if (chartHeight <= 2)
      {
         return;
      }

      feather.fillRect(sectionLeft, chartTop, sectionWidth, chartHeight, Color::BLACK);
      feather.fillRect(sectionLeft, chartBottom - 1, sectionWidth, 1, Color::DARKGRAY);

      uint32_t maxBin = 0;
      for (size_t i = 0; i < histogram.bins(); i++)
      {
         maxBin = std::max(maxBin, histogram.bin(i));
      }

      if (maxBin > 0)
      {
         for (size_t i = 0; i < histogram.bins(); i++)
         {
            int16_t x0 = sectionLeft + static_cast<int16_t>((i * sectionWidth) / histogram.bins());
            int16_t x1 = sectionLeft + static_cast<int16_t>(((i + 1) * sectionWidth) / histogram.bins());
            int16_t barWidth = std::max<int16_t>(1, x1 - x0);
            uint32_t binCount = histogram.bin(i);
            int16_t barHeight = static_cast<int16_t>((binCount * static_cast<uint32_t>(chartHeight - 1)) / maxBin);
            if ((binCount > 0) && (barHeight < 1))
            {
               barHeight = 1;
            }

            if (barHeight > 0)
            {
               feather.fillRect(x0, chartBottom - 1 - barHeight, barWidth, barHeight, barColor);
            }
         }
      }

      String minLabel = "n/a";
      String maxLabel = "n/a";
      if (histogram.count() > 0)
      {
         minLabel = toSignificantString(histogram.min(), labelSignificantDigits);
         maxLabel = toSignificantString(histogram.max(), labelSignificantDigits);
      }

      feather.setCursor(sectionLeft, chartBottom + 1);
      feather.print(minLabel, barColor);

      int16_t maxLabelX = sectionLeft + sectionWidth - static_cast<int16_t>(maxLabel.length() * feather.charW());
      if (maxLabelX < sectionLeft)
      {
         maxLabelX = sectionLeft;
      }
      feather.setCursor(maxLabelX, chartBottom + 1);
      feather.print(maxLabel, barColor);
   }
};
