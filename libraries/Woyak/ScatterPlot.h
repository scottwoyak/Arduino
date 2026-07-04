#pragma once

#include "ArduinoWithDisplay.h"
#include "Color.h"
#include <math.h>

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
	  : _display(display), _x(x), _y(y), _width(width), _height(height), _yAxisWidth(50) {}

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
	  String maxLabelStr = String(plotYMax, 2);
	  String minLabelStr = String(plotYMin, 2);
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

	  // Draw chart background and axes
	  _display->fillRect(chartLeft, chartTop, chartWidth, chartHeight, Color::BLACK);
	  _display->fillRect(chartLeft, chartTop + chartHeight - 1, chartWidth, 1, Color::DARKGRAY);
	  _display->fillRect(chartLeft, chartTop, 1, chartHeight, Color::DARKGRAY);

	  // Create density map
	  size_t pixelCount = static_cast<size_t>(chartWidth) * static_cast<size_t>(chartHeight);
	  uint8_t* density = new uint8_t[pixelCount];
	  if (density == nullptr)
	  {
		 return;
	  }

	  for (size_t i = 0; i < pixelCount; i++)
	  {
		 density[i] = 0;
	  }

	  // Build density map
	  uint8_t maxDensity = 0;
	  for (size_t sampleIndex = 0; sampleIndex < valueCount; sampleIndex++)
	  {
		 int16_t x = static_cast<int16_t>((sampleIndex * static_cast<size_t>(chartWidth - 1)) / max(static_cast<size_t>(1), valueCount - 1));
		 float value = values[sampleIndex];
		 int16_t y = static_cast<int16_t>((plotYMax - value) * static_cast<float>(chartHeight - 1) / ySpan);

		 if (y < 0) y = 0;
		 if (y >= chartHeight) y = chartHeight - 1;

		 size_t densityIndex = static_cast<size_t>(y) * static_cast<size_t>(chartWidth) + static_cast<size_t>(x);
		 if (density[densityIndex] < 255)
		 {
			density[densityIndex]++;
		 }

		 if (density[densityIndex] > maxDensity)
		 {
			maxDensity = density[densityIndex];
		 }
	  }

	  // Render density map with color gradient
	  for (int16_t y = 0; y < chartHeight; y++)
	  {
		 for (int16_t x = 0; x < chartWidth; x++)
		 {
			size_t densityIndex = static_cast<size_t>(y) * static_cast<size_t>(chartWidth) + static_cast<size_t>(x);
			uint8_t value = density[densityIndex];
			if (value == 0)
			{
			   continue;
			}

			float ratio = 0.0f;
			if (maxDensity > 0)
			{
			   ratio = static_cast<float>(value) / static_cast<float>(maxDensity);
			}

			Color color = Color565::blend(Color565::fromRGB(0, 128, 0), Color::GREEN, ratio);
			_display->fillRect(chartLeft + x, chartTop + y, 1, 1, color);
		 }
	  }

	  delete[] density;

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
