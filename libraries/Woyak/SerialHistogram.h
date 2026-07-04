#pragma once

#include <Arduino.h>
#include <math.h>

#include "Histogram.h"
#include "SerialX.h"
#include "Util.h"

/// <summary>
/// Provides histogram-related serial output formatting and utilities.
/// </summary>
class SerialHistogram
{
public:

   /// <summary>
   /// Prints histogram bin ranges and counts to Serial.
   /// </summary>
   /// <param name="title">Histogram section title.</param>
   /// <param name="histogram">Histogram to print.</param>
   /// <param name="decimals">Decimal precision for bin edges.</param>
   static void print(const char* title, const Histogram& histogram, uint8_t decimals)
   {
	  if (histogram.count() == 0)
	  {
		 Serial.println(title);
		 Serial.println("No data");
		 Serial.println();
		 return;
	  }

	  const float minValue = histogram.min();
	  const float maxValue = histogram.max();
	  const float span = maxValue - minValue;

	  uint32_t maxBin = 0;
	  for (size_t i = 0; i < histogram.bins(); i++)
	  {
		 if (histogram.bin(i) > maxBin)
		 {
			maxBin = histogram.bin(i);
		 }
	  }

	  Serial.println(title);
	  SerialX::print("Start", 12);
	  SerialX::print("End", 12);
	  SerialX::print("Count", 10);
	  Serial.println(" Bar");

	  for (size_t i = 0; i < histogram.bins(); i++)
	  {
		 float startValue = minValue + (span * static_cast<float>(i)) / static_cast<float>(histogram.bins());
		 float endValue = minValue + (span * static_cast<float>(i + 1)) / static_cast<float>(histogram.bins());
		 if (i == histogram.bins() - 1)
		 {
			endValue = maxValue;
		 }

		 uint32_t binCount = histogram.bin(i);
		 size_t barLength = 0;
		 if ((binCount > 0) && (maxBin > 0))
		 {
			barLength = (binCount * 40) / maxBin;
			if (barLength == 0)
			{
			   barLength = 1;
			}
		 }

		 String barText;
		 for (size_t j = 0; j < barLength; j++)
		 {
			barText += "*";
		 }

		 SerialX::print(startValue, decimals, 12);
		 SerialX::print(endValue, decimals, 12);
		 SerialX::print(binCount, 10);
		 Serial.print(' ');
		 Serial.println(barText);
	  }

	  SerialX::print("Total Samples", 24);
	  SerialX::println(histogram.count(), 10);
	  Serial.println();
   }
};

