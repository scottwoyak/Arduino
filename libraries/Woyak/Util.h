#pragma once

#include <Arduino.h>
#include <Preferences.h>

#if !defined ( BOARD_HAS_PIN_REMAP ) && !defined ( digitalPinToGPIONumber )
 #define digitalPinToGPIONumber(pin) (pin)
#endif

/// <summary>
/// Utility functions for common operations.
/// </summary>
class Util
{
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

   ///
   /// <summary>
   /// Prints known information about the current board and chip to serial.
   /// </summary>
   ///
   static void printBoardInfo()
   {
      Serial.println("=== Board Info ===");
      Serial.print("Board:            "); Serial.println(ARDUINO_BOARD);
      Serial.print("Chip Model:       "); Serial.println(ESP.getChipModel());
      Serial.print("Chip Revision:    "); Serial.println(ESP.getChipRevision());
      Serial.print("CPU Cores:        "); Serial.println(ESP.getChipCores());
      Serial.print("CPU Freq:         "); Serial.print(ESP.getCpuFreqMHz()); Serial.println(" MHz");
      Serial.print("Flash Size:       "); Serial.print(ESP.getFlashChipSize()); Serial.println(" bytes");
      Serial.print("Flash Speed:      "); Serial.print(ESP.getFlashChipSpeed()); Serial.println(" Hz");
      Serial.print("Heap Size:        "); Serial.print(ESP.getHeapSize()); Serial.println(" bytes");
      Serial.print("Free Heap:        "); Serial.print(ESP.getFreeHeap()); Serial.println(" bytes");
      Serial.print("PSRAM Size:       "); Serial.print(ESP.getPsramSize()); Serial.println(" bytes");
      Serial.print("Free PSRAM:       "); Serial.print(ESP.getFreePsram()); Serial.println(" bytes");
      Serial.print("SDK Version:      "); Serial.println(ESP.getSdkVersion());
      Serial.println("==================");

      Serial.println("=== Default SPI Pins ===");
      Serial.print("SCK:              "); Serial.print(SCK); Serial.print(" (GPIO "); Serial.print(digitalPinToGPIONumber(SCK)); Serial.println(")");
      Serial.print("MOSI:             "); Serial.print(MOSI); Serial.print(" (GPIO "); Serial.print(digitalPinToGPIONumber(MOSI)); Serial.println(")");
      Serial.print("MISO:             "); Serial.print(MISO); Serial.print(" (GPIO "); Serial.print(digitalPinToGPIONumber(MISO)); Serial.println(")");
      Serial.print("SS:               "); Serial.print(SS); Serial.print(" (GPIO "); Serial.print(digitalPinToGPIONumber(SS)); Serial.println(")");
      Serial.println("=========================");

      Serial.println("=== Default I2C Pins ===");
      Serial.print("SDA:              "); Serial.print(SDA); Serial.print(" (GPIO "); Serial.print(digitalPinToGPIONumber(SDA)); Serial.println(")");
      Serial.print("SCL:              "); Serial.print(SCL); Serial.print(" (GPIO "); Serial.print(digitalPinToGPIONumber(SCL)); Serial.println(")");
      Serial.println("=========================");
   }

};
