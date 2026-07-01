#pragma once

#include <Arduino.h>
#include <limits.h>

#include "Histogram.h"

namespace SerialX
{
	/// <summary>
	/// Default serial baud rate.
	/// </summary>
	constexpr uint32_t DEFAULT_BAUD = 115200;

	/// <summary>
	/// Default time to wait for a serial monitor connection in milliseconds.
	/// </summary>
	constexpr uint32_t DEFAULT_TIMEOUT_MS = 2000;

	/// <summary>
	/// Initializes the serial port and waits briefly for a monitor connection.
	/// </summary>
	/// <param name="baud">The serial baud rate.</param>
	/// <param name="timeoutMs">The maximum wait time for a serial connection in milliseconds. Use 0 to skip waiting.</param>
	inline void begin(uint32_t baud = DEFAULT_BAUD, uint32_t timeoutMs = DEFAULT_TIMEOUT_MS)
	{
		Serial.begin(baud);

		if (timeoutMs == 0)
		{
			return;
		}

		const uint32_t start = millis();
		while (!Serial && (millis() - start) < timeoutMs)
		{
			delay(1);
		}
	}

	/// <summary>
	/// Prints a string with optional left space padding.
	/// </summary>
	inline size_t print(const String& text, size_t width = 0)
	{
		size_t printed = 0;
		for (size_t i = text.length(); i < width; i++)
		{
			printed += Serial.print(' ');
		}

		printed += Serial.print(text);
		return printed;
	}

	/// <summary>
	/// Prints a string line with optional left space padding.
	/// </summary>
	inline size_t println(const String& text, size_t width = 0)
	{
		size_t printed = 0;
		for (size_t i = text.length(); i < width; i++)
		{
			printed += Serial.print(' ');
		}

		printed += Serial.println(text);
		return printed;
	}

	/// <summary>
	/// Prints a C-string with optional left space padding.
	/// </summary>
	inline size_t print(const char* text, size_t width = 0)
	{
		return print(String(text), width);
	}

	/// <summary>
	/// Prints a C-string line with optional left space padding.
	/// </summary>
	inline size_t println(const char* text, size_t width = 0)
	{
		return println(String(text), width);
	}

	/// <summary>
	/// Prints a signed integer with optional left space padding.
	/// </summary>
	inline size_t print(long value, size_t width = 0)
	{
		return print(String(value), width);
	}

	/// <summary>
	/// Prints a signed integer with explicit numeric base and optional left space padding.
	/// </summary>
	inline size_t print(long value, uint8_t base, size_t width)
	{
		return print(String(value, base), width);
	}

	/// <summary>
	/// Prints an int with optional left space padding.
	/// </summary>
	inline size_t print(int value, size_t width = 0)
	{
		return print((long)value, width);
	}

	/// <summary>
	/// Prints an unsigned integer with optional left space padding.
	/// </summary>
	inline size_t print(unsigned long value, size_t width = 0)
	{
		return print(String(value), width);
	}

	/// <summary>
	/// Prints an unsigned integer with explicit numeric base and optional left space padding.
	/// </summary>
	inline size_t print(unsigned long value, uint8_t base, size_t width)
	{
		return print(String(value, base), width);
	}

	/// <summary>
	/// Prints a size_t value with optional left space padding.
	/// </summary>
	inline size_t print(size_t value, size_t width = 0)
	{
		return print((unsigned long)value, width);
	}

#if UINT_MAX != SIZE_MAX
	/// <summary>
	/// Prints an unsigned int with optional left space padding.
	/// </summary>
	inline size_t print(unsigned int value, size_t width = 0)
	{
		return print((unsigned long)value, width);
	}
#endif

	/// <summary>
	/// Prints a floating-point number with optional left space padding.
	/// </summary>
	inline size_t print(float value, size_t width = 0)
	{
		return print(String(value), width);
	}

	/// <summary>
	/// Prints a floating-point number with explicit decimal places and optional left space padding.
	/// </summary>
	inline size_t print(float value, uint8_t decimals, size_t width)
	{
		return print(String(value, (unsigned int)decimals), width);
	}

	/// <summary>
	/// Prints a double-precision number with optional left space padding.
	/// </summary>
	inline size_t print(double value, size_t width = 0)
	{
		return print(String(value), width);
	}

	/// <summary>
	/// Prints a double-precision number with explicit decimal places and optional left space padding.
	/// </summary>
	inline size_t print(double value, uint8_t decimals, size_t width)
	{
		return print(String(value, (unsigned int)decimals), width);
	}

	/// <summary>
	/// Prints a signed integer line with optional left space padding.
	/// </summary>
	inline size_t println(long value, size_t width = 0)
	{
		return println(String(value), width);
	}

	/// <summary>
	/// Prints a signed integer line with explicit numeric base and optional left space padding.
	/// </summary>
	inline size_t println(long value, uint8_t base, size_t width)
	{
		return println(String(value, base), width);
	}

	/// <summary>
	/// Prints an int line with optional left space padding.
	/// </summary>
	inline size_t println(int value, size_t width = 0)
	{
		return println((long)value, width);
	}

	/// <summary>
	/// Prints an unsigned integer line with optional left space padding.
	/// </summary>
	inline size_t println(unsigned long value, size_t width = 0)
	{
		return println(String(value), width);
	}

	/// <summary>
	/// Prints an unsigned integer line with explicit numeric base and optional left space padding.
	/// </summary>
	inline size_t println(unsigned long value, uint8_t base, size_t width)
	{
		return println(String(value, base), width);
	}

	/// <summary>
	/// Prints a size_t line with optional left space padding.
	/// </summary>
	inline size_t println(size_t value, size_t width = 0)
	{
		return println((unsigned long)value, width);
	}

#if UINT_MAX != SIZE_MAX
	/// <summary>
	/// Prints an unsigned int line with optional left space padding.
	/// </summary>
	inline size_t println(unsigned int value, size_t width = 0)
	{
		return println((unsigned long)value, width);
	}
#endif

	/// <summary>
	/// Prints a floating-point number line with optional left space padding.
	/// </summary>
	inline size_t println(float value, size_t width = 0)
	{
		return println(String(value), width);
	}

	/// <summary>
	/// Prints a floating-point number line with explicit decimal places and optional left space padding.
	/// </summary>
	inline size_t println(float value, uint8_t decimals, size_t width)
	{
		return println(String(value, (unsigned int)decimals), width);
	}

	/// <summary>
	/// Prints a double-precision line with optional left space padding.
	/// </summary>
	inline size_t println(double value, size_t width = 0)
	{
		return println(String(value), width);
	}

	/// <summary>
	/// Prints a double-precision line with explicit decimal places and optional left space padding.
	/// </summary>
	inline size_t println(double value, uint8_t decimals, size_t width)
	{
		return println(String(value, (unsigned int)decimals), width);
	}

	/// <summary>
	/// Prints histogram bin ranges and counts to Serial.
	/// </summary>
	/// <typeparam name="BIN_COUNT">Histogram bin count.</typeparam>
	/// <param name="title">Histogram section title.</param>
	/// <param name="histogram">Histogram values to print.</param>
	/// <param name="decimals">Decimal precision for bin edges.</param>
	template<size_t BIN_COUNT>
	inline void printHistogramBins(const char* title, const Histogram<BIN_COUNT>& histogram, uint8_t decimals)
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
			float startValue = minValue + (span * i) / histogram.bins();
			float endValue = minValue + (span * (i + 1)) / histogram.bins();
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
}
