#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "I2CMutex.h"
#include "Util.h"

///
/// <summary>
/// Driver for the Texas Instruments ADS1115 16-bit I2C analog-to-digital converter.
/// Supports configurable input multiplexer channels (differential or single-ended),
/// programmable gain amplifier (PGA) full-scale voltage ranges, and conversion data rates
/// up to 860 samples per second (SPS) in continuous conversion mode.
/// </summary>
///
class ADS1115
{
public:
   ///
   /// <summary>
   /// Default and alternative I2C addresses selectable via the ADDR pin.
   /// </summary>
   ///
   static constexpr uint8_t ADDRESS_GND = 0x48;
   static constexpr uint8_t ADDRESS_VDD = 0x49;
   static constexpr uint8_t ADDRESS_SDA = 0x4A;
   static constexpr uint8_t ADDRESS_SCL = 0x4B;

   ///
   /// <summary>
   /// Input multiplexer channel configuration (bits [14:12] of the Config register).
   /// </summary>
   ///
   enum class Mux : uint16_t
   {
      DIFF_A0_A1 = 0x0000,
      DIFF_A0_A3 = 0x1000,
      DIFF_A1_A3 = 0x2000,
      DIFF_A2_A3 = 0x3000,
      SINGLE_A0 = 0x4000,
      SINGLE_A1 = 0x5000,
      SINGLE_A2 = 0x6000,
      SINGLE_A3 = 0x7000,
   };

   ///
   /// <summary>
   /// Programmable gain amplifier full-scale voltage range (bits [11:9] of the Config register).
   /// </summary>
   ///
   enum class Gain : uint16_t
   {
      FS_6_144V = 0x0000,
      FS_4_096V = 0x0200,
      FS_2_048V = 0x0400,
      FS_1_024V = 0x0600,
      FS_0_512V = 0x0800,
      FS_0_256V = 0x0A00,
   };

   ///
   /// <summary>
   /// Continuous conversion data rate in samples per second (bits [7:5] of the Config register).
   /// </summary>
   ///
   enum class DataRate : uint16_t
   {
      SPS_8 = 0x0000,
      SPS_16 = 0x0020,
      SPS_32 = 0x0040,
      SPS_64 = 0x0060,
      SPS_128 = 0x0080,
      SPS_250 = 0x00A0,
      SPS_475 = 0x00C0,
      SPS_860 = 0x00E0,
   };

private:
   static constexpr uint8_t REG_CONVERSION = 0x00;
   static constexpr uint8_t REG_CONFIG = 0x01;
   static constexpr uint16_t MODE_CONTINUOUS = 0x0000;
   static constexpr uint16_t COMP_DISABLE = 0x0003;
   static constexpr uint32_t DEFAULT_I2C_CLOCK_HZ = 400000;

   uint8_t _address = ADDRESS_GND;
   Mux _mux = Mux::DIFF_A0_A1;
   Gain _gain = Gain::FS_2_048V;
   DataRate _dataRate = DataRate::SPS_860;
   float _voltsPerCount = 2.048f / 32768.0f;

   ///
   /// <summary>
   /// Computes the volts-per-LSB scale factor for the given PGA gain setting.
   /// </summary>
   /// <param name="gain">PGA gain setting.</param>
   /// <returns>Volts per raw ADC count.</returns>
   ///
   static constexpr float _computeVoltsPerCount(Gain gain)
   {
      switch (gain)
      {
         case Gain::FS_6_144V:
            return 6.144f / 32768.0f;
         case Gain::FS_4_096V:
            return 4.096f / 32768.0f;
         case Gain::FS_2_048V:
            return 2.048f / 32768.0f;
         case Gain::FS_1_024V:
            return 1.024f / 32768.0f;
         case Gain::FS_0_512V:
            return 0.512f / 32768.0f;
         case Gain::FS_0_256V:
            return 0.256f / 32768.0f;
         default:
            return 2.048f / 32768.0f;
      }
   }

public:
   ///
   /// <summary>
   /// Creates an ADS1115 ADC driver with the specified I2C address, input channel mux,
   /// PGA gain range, and continuous sample rate.
   /// </summary>
   /// <param name="address">7-bit I2C address (default 0x48 when ADDR is grounded).</param>
   /// <param name="mux">Input multiplexer channel selection.</param>
   /// <param name="gain">Programmable gain amplifier full-scale range.</param>
   /// <param name="dataRate">Conversion data rate in samples per second.</param>
   ///
   explicit ADS1115(
      uint8_t address = ADDRESS_GND,
      Mux mux = Mux::DIFF_A0_A1,
      Gain gain = Gain::FS_2_048V,
      DataRate dataRate = DataRate::SPS_860)
   {
      _address = address;
      _mux = mux;
      _gain = gain;
      _dataRate = dataRate;
      _voltsPerCount = _computeVoltsPerCount(gain);
   }

   ///
   /// <summary>
   /// Configures the I2C clock for fast reads, writes the continuous conversion configuration
   /// to the ADS1115 Config register, and leaves the register pointer aimed at the Conversion
   /// register for fast 2-byte reads.
   /// </summary>
   /// <param name="i2cClockHz">I2C bus clock frequency in Hz (default 400 kHz).</param>
   /// <returns>True if the ADS1115 acknowledged the I2C configuration; false otherwise.</returns>
   ///
   bool begin(uint32_t i2cClockHz = DEFAULT_I2C_CLOCK_HZ)
   {
      I2CScopedLock lock;
      Wire.setClock(i2cClockHz);
      return setMux(_mux);
   }

   ///
   /// <summary>
   /// Switches the ADS1115 input multiplexer channel in continuous conversion mode and
   /// re-aims the register pointer at the Conversion register for subsequent 2-byte reads.
   /// </summary>
   /// <param name="mux">The new input multiplexer channel selection.</param>
   /// <returns>True if the ADS1115 acknowledged the configuration; false otherwise.</returns>
   ///
   bool setMux(Mux mux)
   {
      I2CScopedLock lock;
      _mux = mux;

      uint16_t config = static_cast<uint16_t>(_mux) |
                        static_cast<uint16_t>(_gain) |
                        MODE_CONTINUOUS |
                        static_cast<uint16_t>(_dataRate) |
                        COMP_DISABLE;

      Wire.beginTransmission(_address);
      Wire.write(REG_CONFIG);
      Wire.write(static_cast<uint8_t>(config >> 8));
      Wire.write(static_cast<uint8_t>(config & 0xFF));
      if (Wire.endTransmission() != 0)
      {
         return false;
      }

      Wire.beginTransmission(_address);
      Wire.write(REG_CONVERSION);
      return Wire.endTransmission() == 0;
   }

   ///
   /// <summary>
   /// Gets the currently configured input multiplexer channel.
   /// </summary>
   /// <returns>Current Mux setting.</returns>
   ///
   Mux mux() const
   {
      return _mux;
   }

   ///
   /// <summary>
   /// Reads the latest signed 16-bit conversion count from the ADS1115 conversion register.
   /// </summary>
   /// <param name="rawCounts">Receives the signed 16-bit ADC count.</param>
   /// <returns>True if 2 bytes were read from the ADS1115; false on I2C read failure.</returns>
   ///
   bool readCounts(int16_t* rawCounts) const
   {
      ASSERT(rawCounts != nullptr);

      I2CScopedLock lock(0);
      if (!lock.locked())
      {
         return false;
      }

      if (Wire.requestFrom(_address, static_cast<uint8_t>(2)) != 2)
      {
         return false;
      }

      *rawCounts = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
      return true;
   }

   ///
   /// <summary>
   /// Reads the latest conversion sample from the ADS1115 and converts it to volts using
   /// the configured PGA gain scale.
   /// </summary>
   /// <param name="volts">Receives the measured voltage in volts.</param>
   /// <returns>True if the sample was read successfully; false on I2C read failure.</returns>
   ///
   bool readVolts(float* volts) const
   {
      ASSERT(volts != nullptr);

      int16_t rawCounts = 0;
      if (!readCounts(&rawCounts))
      {
         return false;
      }

      *volts = rawCounts * _voltsPerCount;
      return true;
   }

   ///
   /// <summary>
   /// Gets the conversion period in microseconds corresponding to the configured data rate.
   /// </summary>
   /// <returns>Conversion interval in microseconds.</returns>
   ///
   uint32_t conversionIntervalMicros() const
   {
      switch (_dataRate)
      {
         case DataRate::SPS_8:
            return 125000;
         case DataRate::SPS_16:
            return 62500;
         case DataRate::SPS_32:
            return 31250;
         case DataRate::SPS_64:
            return 15625;
         case DataRate::SPS_128:
            return 7813;
         case DataRate::SPS_250:
            return 4000;
         case DataRate::SPS_475:
            return 2105;
         case DataRate::SPS_860:
         default:
            return 1163;
      }
   }
};
