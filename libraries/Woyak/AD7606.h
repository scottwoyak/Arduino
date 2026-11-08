#pragma once

#include <Arduino.h>
#include <sstream>

#include "Logger.h"
#include "Util.h"

///
/// <summary>
/// Driver for the Analog Devices AD7606 8-channel, 16-bit simultaneous-sampling bipolar ADC
/// operated in 4-wire serial (SPI/DOUTA) mode.
///
/// A single pulse on CONVST (tied to CONVSTA and CONVSTB) latches all 8 analog input channels
/// (V1..V8) at the exact same microsecond via 8 independent track-and-hold amplifiers and
/// converts them in ~4 microseconds. The 8 signed 16-bit conversion results are clocked out
/// sequentially over SCLK/DOUTA while CS is held low.
/// </summary>
///
class AD7606
{
public:
   ///
   /// <summary>
   /// Number of simultaneous analog input channels on a single AD7606.
   /// </summary>
   ///
   static constexpr size_t NUM_CHANNELS = 8;

   ///
   /// <summary>
   /// Selectable bipolar analog input voltage range (controlled by the RANGE pin on the module).
   /// </summary>
   ///
   enum class Range : uint8_t
   {
      BIPOLAR_5V = 0,
      BIPOLAR_10V = 1,
   };

private:
   static constexpr uint8_t CONVERSION_WAIT_MICROS = 5;
   static constexpr uint8_t RESET_PULSE_MICROS = 5;

   uint8_t _pinConvst = 2;
   uint8_t _pinSclk = 3;
   uint8_t _pinMiso = 14;
   uint8_t _pinCs = 21;
   int8_t _pinBusy = -1;
   int8_t _pinReset = -1;
   Range _range = Range::BIPOLAR_5V;
   float _voltsPerCount = 5.0f / 32768.0f;

   ///
   /// <summary>
   /// Computes the volts-per-LSB scale factor for the given bipolar input range.
   /// </summary>
   /// <param name="range">Bipolar input voltage range.</param>
   /// <returns>Volts per raw signed 16-bit ADC count.</returns>
   ///
   static constexpr float _computeVoltsPerCount(Range range)
   {
      return (range == Range::BIPOLAR_10V) ? (10.0f / 32768.0f) : (5.0f / 32768.0f);
   }

public:
   ///
   /// <summary>
   /// Creates an AD7606 driver using the specified control and serial data pins.
   /// By default uses the 4 external GPIO pins (2, 3, 14, 21) available on the Hosyond
   /// ESP32-S3 display board.
   /// </summary>
   /// <param name="pinConvst">GPIO pin connected to AD7606 CONVSTA+CONVSTB (default GPIO2).</param>
   /// <param name="pinSclk">GPIO pin connected to AD7606 RD/SCLK (default GPIO3).</param>
   /// <param name="pinMiso">GPIO pin connected to AD7606 DB7/DOUTA (default GPIO14).</param>
   /// <param name="pinCs">GPIO pin connected to AD7606 CS (default GPIO21).</param>
   /// <param name="range">Bipolar input range setting (default +/-5V).</param>
   /// <param name="pinBusy">Optional GPIO pin connected to AD7606 BUSY (-1 if not wired).</param>
   /// <param name="pinReset">Optional GPIO pin connected to AD7606 RST (-1 if tied low on board).</param>
   ///
   explicit AD7606(
      uint8_t pinConvst = 2,
      uint8_t pinSclk = 3,
      uint8_t pinMiso = 14,
      uint8_t pinCs = 21,
      Range range = Range::BIPOLAR_5V,
      int8_t pinBusy = -1,
      int8_t pinReset = -1)
   {
      _pinConvst = pinConvst;
      _pinSclk = pinSclk;
      _pinMiso = pinMiso;
      _pinCs = pinCs;
      _range = range;
      _pinBusy = pinBusy;
      _pinReset = pinReset;
      _voltsPerCount = _computeVoltsPerCount(range);
   }

   ///
   /// <summary>
   /// Configures the ESP32 GPIO pins for the AD7606 serial interface, pulses RESET if wired,
   /// and performs an initial conversion read to verify communication.
   /// </summary>
   /// <returns>True if the AD7606 interface initialized and completed an initial conversion.</returns>
   ///
   bool begin()
   {
      pinMode(_pinConvst, OUTPUT);
      digitalWrite(_pinConvst, HIGH);

      pinMode(_pinCs, OUTPUT);
      digitalWrite(_pinCs, HIGH);

      pinMode(_pinSclk, OUTPUT);
      digitalWrite(_pinSclk, HIGH);

      pinMode(_pinMiso, INPUT_PULLUP);

      if (_pinBusy >= 0)
      {
         pinMode(_pinBusy, INPUT_PULLUP);
      }

      delay(2);

      if (_pinReset >= 0)
      {
         pinMode(_pinReset, OUTPUT);
         digitalWrite(_pinReset, LOW);
         delayMicroseconds(RESET_PULSE_MICROS);
         digitalWrite(_pinReset, HIGH);
         delayMicroseconds(RESET_PULSE_MICROS);
         digitalWrite(_pinReset, LOW);
         delayMicroseconds(RESET_PULSE_MICROS);
      }

      int16_t counts[NUM_CHANNELS] = {};
      if (!readAllCounts(counts))
      {
         Serial.printf(
            "AD7606::begin failed: BUSY(GPIO%d)=%d MISO(GPIO%d)=%d\n",
            _pinBusy,
            _pinBusy >= 0 ? digitalRead(_pinBusy) : -1,
            _pinMiso,
            digitalRead(_pinMiso));
         return false;
      }

      Serial.printf(
         "AD7606::begin OK: BUSY(GPIO%d)=%d MISO(GPIO%d)=%d ch0=%d\n",
         _pinBusy,
         _pinBusy >= 0 ? digitalRead(_pinBusy) : -1,
         _pinMiso,
         digitalRead(_pinMiso),
         counts[0]);
      return true;
   }

   ///
   /// <summary>
   /// Triggers a simultaneous conversion across all 8 channels (V1..V8) and clocks out
   /// the 8 signed 16-bit ADC counts over the serial DOUTA interface.
   /// </summary>
   /// <param name="rawCounts">Destination array of 8 signed 16-bit integers.</param>
   /// <returns>True if the conversion completed and 8 channel counts were read.</returns>
   ///
   bool readAllCounts(int16_t* rawCounts) const
   {
      ASSERT(rawCounts != nullptr);

      // Pulse CONVST low then high to latch all 8 track-and-hold amplifiers simultaneously
      digitalWrite(_pinConvst, LOW);
      delayMicroseconds(1);
      digitalWrite(_pinConvst, HIGH);

      if (_pinBusy >= 0)
      {
         // Wait up to 20 us for BUSY to fall LOW (~4 us conversion time with no oversampling).
         delayMicroseconds(1);
         uint32_t startMicros = micros();
         while (digitalRead(_pinBusy) == HIGH)
         {
            if ((micros() - startMicros) > 20)
            {
               return false;
            }
         }
      }
      else
      {
         delayMicroseconds(CONVERSION_WAIT_MICROS);
      }

      digitalWrite(_pinCs, LOW);

      // Per AD7606 datasheet (Serial Read Operation):
      // - CS falling edge takes DOUTA out of three-state and outputs the MSB (Bit 15).
      // - SCLK falling edge latches/samples the current bit while SCLK is LOW.
      // - SCLK rising edge clocks the next bit onto DOUTA.
      for (size_t ch = 0; ch < NUM_CHANNELS; ch++)
      {
         uint16_t word = 0;
         for (uint8_t bit = 0; bit < 16; bit++)
         {
            digitalWrite(_pinSclk, LOW);
            word = static_cast<uint16_t>((word << 1) | (digitalRead(_pinMiso) ? 1U : 0U));
            digitalWrite(_pinSclk, HIGH);
         }
         rawCounts[ch] = static_cast<int16_t>(word);
      }

      digitalWrite(_pinCs, HIGH);
      return true;
   }

   ///
   /// <summary>
   /// Triggers a simultaneous conversion across all 8 channels (V1..V8) and converts
   /// the 8 readings to volts.
   /// </summary>
   /// <param name="volts">Destination array of 8 float voltage values.</param>
   /// <returns>True if the conversion completed and 8 channel voltages were read.</returns>
   ///
   bool readAllVolts(float* volts) const
   {
      ASSERT(volts != nullptr);

      int16_t counts[NUM_CHANNELS];
      if (!readAllCounts(counts))
      {
         return false;
      }

      for (size_t ch = 0; ch < NUM_CHANNELS; ch++)
      {
         volts[ch] = counts[ch] * _voltsPerCount;
      }

      return true;
   }

   ///
   /// <summary>
   /// Gets the volts-per-LSB scale factor for the active bipolar input range.
   /// </summary>
   /// <returns>Volts per ADC count.</returns>
   ///
   float voltsPerCount() const
   {
      return _voltsPerCount;
   }
};
