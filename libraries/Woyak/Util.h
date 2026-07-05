#pragma once

#include <Arduino.h>
#include <Preferences.h>

/// <summary>
/// Utility functions for common operations.
/// </summary>
class Util
{

   /*
   const char* WiFiStatus2Str(uint8_t status) {
      switch (status) {
      case WL_NO_SHIELD: return "WL_NO_SHIELD";
      case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
      case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
      case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
      case WL_CONNECTED: return "WL_CONNECTED";
      case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
      case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
      case WL_DISCONNECTED: return "WL_DISCONNECTED";
      default: return "UNKNOWN";
      }
   }

   bool connectToWifi(const char* ssid, const char* pass, ulong timeoutMs = 0) {
      Serial.print("Connecting to ");
      Serial.print(ssid);

      WiFi.setPins(8, 7, 4, 2);
      int status = WiFi.begin(ssid, pass);

      Stopwatch sw;
      while (status != WL_CONNECTED) {

         if (timeoutMs > 0 && sw.elapsedMillis() > timeoutMs) {
            break;
         }

         // wait for connection:
         Serial.print(".");
         delay(500);
      }
      Serial.println();

      if (status == WL_CONNECTED) {
         Serial.print("Connected to ");
         Serial.println(ssid);
         return true;
      }
      else {
         Serial.println("Connection failed");
         return false;
      }
   }
   */

public:
   /// <summary>
   /// Reads ADC input and converts to voltage accounting for voltage divider.
   /// </summary>
   /// <param name="pin">Analog input pin</param>
   /// <param name="resolution">ADC resolution in counts (ESP32: 4096, others: 1024)</param>
   /// <param name="voltageDivider">Voltage divider ratio (e.g., 2.0 for 2:1 divider)</param>
   /// <returns>Voltage in volts</returns>
   static float readVolts(uint8_t pin, uint16_t resolution = 4096, float voltageDivider = 2.0f)
   {
      float volts = analogRead(pin);
      volts *= voltageDivider; // compensate for voltage divider
      volts *= 3.3f; // multiply by 3.3V, our reference voltage
      volts /= resolution; // convert to voltage
      return volts;
   }

   /// <summary>
   /// Converts battery voltage to charge percentage.
   /// </summary>
   /// <param name="volts">Battery voltage</param>
   /// <param name="fullChargeVolts">Voltage when fully charged (e.g., 4.1V for Li-Ion)</param>
   /// <returns>Charge percentage 0-100%</returns>
   /// <remarks>
   /// Uses linear interpolation between MIN_VOLTS (3.3V) and fullChargeVolts.
   /// Result is clamped to 0-100%.
   /// </remarks>
   static float voltsToPercent(float volts, float fullChargeVolts = 4.1f)
   {
      const float MIN_VOLTS = 3.3f;
      float percent = 100.0f * (volts - MIN_VOLTS) / (fullChargeVolts - MIN_VOLTS);
      percent = constrain(percent, 0.0f, 100.0f);
      return percent;
   }

   /// <summary>
   /// Calculates elapsed time between two clock readings, handling wraparound.
   /// Uses unsigned arithmetic: if end &lt; start (clock wrapped), the result
   /// is still correct due to modulo 2^32 arithmetic.
   /// </summary>
   /// <param name="start">Start timestamp from micros() or millis()</param>
   /// <param name="end">End timestamp from micros() or millis()</param>
   /// <returns>Elapsed time in the same units as the input timestamps</returns>
	static unsigned long getSpan(unsigned long start, unsigned long end)
	{
		return end - start;
	}

	/// <summary>
	/// Calculates microseconds elapsed since the given starting time.
	/// </summary>
	/// <param name="start">Starting timestamp from a prior micros() call</param>
	/// <returns>Microseconds elapsed</returns>
	static unsigned long microsSince(unsigned long start)
	{
		return getSpan(start, micros());
	}

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
	/// Resets the device after an optional delay.
	/// </summary>
	/// <param name="delaySecs">Seconds to delay before reset (default 0.0)</param>
	/// <remarks>
	/// Calls ESP.restart() for ESP32. Behavior may differ on other boards.
	/// This function does not return.
	/// </remarks>
	static void reset(float delaySecs = 0.0f)
	{
		delay(static_cast<unsigned long>(1000.0f * delaySecs));

		// TODO this is for the ESP32 which complains about the standard resetFunc() method of reset. We'll
		// need other techniques for different boards.
		ESP.restart();
	}

	/// <summary>
	/// Saves a halt reason to a preferences object.
	/// </summary>
	static void setHaltReason(const String& reason)
	{
		Preferences preferences;
		preferences.begin("Woyak", false);
		preferences.putString("halt", reason);
		preferences.end();
	}

	/// <summary>
	/// Checks for a previous halt reason in a preferences object and prints it to Serial if found.
	/// </summary>
	static void checkHaltReason()
	{
		Preferences preferences;
		preferences.begin("Woyak", false);
		// Note: Not all implementation of Preferences have isKey, but ESP32 and PreferencesFlash do.
		if (preferences.isKey("halt"))
		{
			String reason = preferences.getString("halt", "");
			if (reason.length() > 0)
			{
				Serial.print("Previous halt reason: ");
				Serial.println(reason);
			}
			preferences.remove("halt");
		}
		preferences.end();
	}

};
