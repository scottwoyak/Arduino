#pragma once

#include <Arduino.h>
#include <limits.h>
#include "Util.h"

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

		if (timeoutMs != 0)
		{
			uint32_t start = millis();
			while (!Serial && (millis() - start) < timeoutMs)
			{
				delay(10);
			}
		}

      // The first real print after the wait above can still get silently dropped
      // (native USB CDC boards need an initial empty println() to prime the
      // connection; UART-bridge boards need a brief delay while the OS finishes
      // enumerating the port). Both fixes are cheap and harmless on every board,
      // so just always do both rather than trying to detect the exact USB mode.
      delay(1000);
      Serial.println();

		Util::checkTheLastShutdownReason();
	}

	/// <summary>
	/// Prints a label, then blocks until a line of text is entered over Serial, echoing
	/// it back and returning it trimmed of leading/trailing whitespace. Used for simple
	/// interactive setup-time prompts (e.g. site/location configuration).
	/// </summary>
	/// <param name="label">The prompt label to print before waiting for input (e.g. "Enter site: ").</param>
	/// <returns>The entered text, trimmed of leading/trailing whitespace.</returns>
	inline String prompt(const char* label)
	{
		Serial.print(label);

		while (!Serial.available())
		{
			delay(10);
		}
		String input = Serial.readStringUntil('\n');
		input.trim();
		Serial.println(input);

		return input;
	}

	/// <summary>
	/// Prints a label, then blocks until a line of text is entered over Serial, echoing
	/// it back and returning it trimmed of leading/trailing whitespace. Used for simple
	/// interactive setup-time prompts (e.g. site/location configuration).
	/// </summary>
	/// <param name="label">The prompt label to print before waiting for input (e.g. "Enter site: ").</param>
	/// <returns>The entered text, trimmed of leading/trailing whitespace.</returns>
	inline String prompt(const String& label)
	{
		return prompt(label.c_str());
	}

	/// <summary>
	/// Prints a label, then blocks until a valid whole number within [min, max] is entered
	/// over Serial, reprompting on invalid/out-of-range input. Used for simple interactive
	/// setup-time menu selections (e.g. choosing a numbered option from a list).
	/// </summary>
	/// <param name="label">The prompt label to print before waiting for input (e.g. "Enter selection (1-2): ").</param>
	/// <param name="min">The minimum acceptable value, inclusive.</param>
	/// <param name="max">The maximum acceptable value, inclusive.</param>
	/// <returns>The entered number, guaranteed to be within [min, max].</returns>
	inline long promptForInt(const char* label, long min, long max)
	{
		while (true)
		{
			String input = prompt(label);

			bool isNumeric = input.length() > 0;
			for (size_t i = 0; i < input.length(); i++)
			{
				if (!isDigit(input[i]))
				{
					isNumeric = false;
					break;
				}
			}

			if (isNumeric)
			{
				long value = input.toInt();
				if (value >= min && value <= max)
				{
					return value;
				}
			}

			Serial.println("Invalid selection, try again.");
		}
	}

	/// <summary>
	/// Prints a label, then blocks until a valid whole number within [min, max] is entered
	/// over Serial, reprompting on invalid/out-of-range input. Used for simple interactive
	/// setup-time menu selections (e.g. choosing a numbered option from a list).
	/// </summary>
	/// <param name="label">The prompt label to print before waiting for input (e.g. "Enter selection (1-2): ").</param>
	/// <param name="min">The minimum acceptable value, inclusive.</param>
	/// <param name="max">The maximum acceptable value, inclusive.</param>
	/// <returns>The entered number, guaranteed to be within [min, max].</returns>
	inline long promptForInt(const String& label, long min, long max)
	{
		return promptForInt(label.c_str(), min, max);
	}

	/// <summary>
	/// Prints a header, then a numbered list of options, then blocks until a valid
	/// selection (1-based) is entered over Serial, reprompting on invalid/out-of-range
	/// input. Used to simplify interactive setup-time menu selections (e.g. choosing a
	/// numbered option from a list of sites/buckets/locations).
	/// </summary>
	/// <param name="header">Text printed before the numbered list (e.g. "Select a site:").</param>
	/// <param name="options">The list of option descriptions to print and choose from.</param>
	/// <param name="count">The number of entries in options.</param>
	/// <returns>The 0-based index into options for the chosen entry.</returns>
	inline size_t promptForOption(const char* header, const String options[], size_t count)
	{
		Serial.println(header);
		for (size_t i = 0; i < count; i++)
		{
			Serial.print("  ");
			Serial.print(i + 1);
			Serial.print(": ");
			Serial.println(options[i]);
		}

		String label = "Enter selection (1-" + String(count) + "): ";
		long selection = promptForInt(label, 1, (long)count);

		return (size_t)(selection - 1);
	}

	/// <summary>
	/// Prints a header, then a numbered list of options (marking defaultIndex as the
	/// current default), then waits up to timeoutMs for a selection to be entered over
	/// Serial. If no input arrives before the timeout, or the entered value isn't a
	/// valid selection, defaultIndex is returned instead. Used so an automatically
	/// triggered prompt (e.g. because a Serial monitor is attached) can't hang the
	/// device forever if nobody responds.
	/// </summary>
	/// <param name="count">The number of selectable options.</param>
	/// <param name="defaultIndex">The 0-based index used if the timeout elapses or the input is invalid.</param>
	/// <param name="timeoutMs">How long to wait for a selection before falling back to defaultIndex, in milliseconds.</param>
	/// <returns>The 0-based index for the chosen (or default) entry.</returns>
	inline size_t readSelectionWithTimeout(size_t count, size_t defaultIndex, uint32_t timeoutMs)
	{
		Serial.print("Enter selection (1-");
		Serial.print(count);
		Serial.print("), defaults to ");
		Serial.print(defaultIndex + 1);
		Serial.print(" after ");
		Serial.print(timeoutMs / 1000);
		Serial.println("s: ");

		uint32_t start = millis();
		while (!Serial.available())
		{
			if (millis() - start >= timeoutMs)
			{
				Serial.println("No input - using default.");
				return defaultIndex;
			}
			delay(10);
		}

		String input = Serial.readStringUntil('\n');
		input.trim();
		Serial.println(input);

		if (input.length() == 0)
		{
			Serial.println("Using default.");
			return defaultIndex;
		}

		bool isNumeric = true;
		for (size_t i = 0; i < input.length(); i++)
		{
			if (!isDigit(input[i]))
			{
				isNumeric = false;
				break;
			}
		}

		if (isNumeric)
		{
			long selection = input.toInt();
			if (selection >= 1 && selection <= (long)count)
			{
				return (size_t)(selection - 1);
			}
		}

		Serial.println("Invalid selection - using default.");
		return defaultIndex;
	}

	/// <summary>
	/// Prints a label, then blocks until a valid floating-point number within [min, max] is
	/// entered over Serial, reprompting on invalid/out-of-range input. Used for simple
	/// interactive setup-time prompts (e.g. entering a calibration value).
	/// </summary>
	/// <param name="label">The prompt label to print before waiting for input (e.g. "Enter offset: ").</param>
	/// <param name="min">The minimum acceptable value, inclusive.</param>
	/// <param name="max">The maximum acceptable value, inclusive.</param>
	/// <returns>The entered number, guaranteed to be within [min, max].</returns>
	inline float promptForFloat(const char* label, float min, float max)
	{
		while (true)
		{
			String input = prompt(label);

			char* end = nullptr;
			float value = strtof(input.c_str(), &end);
			bool isNumeric = input.length() > 0 && end != input.c_str() && *end == '\0';

			if (isNumeric && value >= min && value <= max)
			{
				return value;
			}

			Serial.println("Invalid value, try again.");
		}
	}

	/// <summary>
	/// Prints a label, then blocks until a valid floating-point number within [min, max] is
	/// entered over Serial, reprompting on invalid/out-of-range input. Used for simple
	/// interactive setup-time prompts (e.g. entering a calibration value).
	/// </summary>
	/// <param name="label">The prompt label to print before waiting for input (e.g. "Enter offset: ").</param>
	/// <param name="min">The minimum acceptable value, inclusive.</param>
	/// <param name="max">The maximum acceptable value, inclusive.</param>
	/// <returns>The entered number, guaranteed to be within [min, max].</returns>
	inline float promptForFloat(const String& label, float min, float max)
	{
		return promptForFloat(label.c_str(), min, max);
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
	/// Prints a blank line.
	/// </summary>
	inline size_t println()
	{
		size_t printed = Serial.println();

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
};
