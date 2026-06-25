#pragma once

#include <Arduino.h>

namespace SerialX
{
	/// <summary>
	/// Default serial baud rate.
	/// </summary>
	constexpr uint32_t DEFAULT_BAUD = 115200;

	/// <summary>
	/// Default time to wait for a serial monitor connection in milliseconds.
	/// </summary>
	constexpr uint32_t DEFAULT_TIMEOUT_MS = 1000;

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
	/// Prints a signed integer with optional left space padding.
	/// </summary>
	inline size_t print(long value, size_t width = 0)
	{
		return print(String(value), width);
	}

	/// <summary>
	/// Prints an unsigned integer with optional left space padding.
	/// </summary>
	inline size_t print(unsigned long value, size_t width = 0)
	{
		return print(String(value), width);
	}

	/// <summary>
	/// Prints a floating-point number with optional left space padding.
	/// </summary>
	inline size_t print(float value, size_t width = 0)
	{
		return print(String(value), width);
	}

	/// <summary>
	/// Prints a signed integer line with optional left space padding.
	/// </summary>
	inline size_t println(long value, size_t width = 0)
	{
		return println(String(value), width);
	}

	/// <summary>
	/// Prints an unsigned integer line with optional left space padding.
	/// </summary>
	inline size_t println(unsigned long value, size_t width = 0)
	{
		return println(String(value), width);
	}

	/// <summary>
	/// Prints a floating-point number line with optional left space padding.
	/// </summary>
	inline size_t println(float value, size_t width = 0)
	{
		return println(String(value), width);
	}
}
