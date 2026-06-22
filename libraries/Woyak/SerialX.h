#pragma once

namespace SerialX
{
	/// <summary>
	/// Default serial baud rate.
	/// </summary>
	constexpr uint32_t DEFAULT_BAUD = 115200;

	/// <summary>
	/// Default time to wait for a serial monitor connection in milliseconds.
	/// </summary>
	constexpr uint32_t DEFAULT_TIMEOUT_MS = 500;

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
}