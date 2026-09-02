#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "RollingAverage.h"

///
/// <summary>
/// Driver for the QMC5883P magnetometer, as found on HW-127/GY-273 breakouts. This chip is a
/// QMC5883L-compatible clone that moved to I2C address 0x2C (instead of the QMC5883L's 0x0D
/// or the HMC5883L's 0x1E) and shifted its register map up by one address relative to the
/// original QMC5883L layout (a chip-ID byte occupies 0x00, which the QMC5883L doesn't have).
/// It also does not reliably support I2C repeated-start, so all reads use a full stop between
/// writing the register pointer and reading it back.
/// </summary>
/// <remarks>
/// Each axis is smoothed with a rolling average over a configurable number of samples
/// (default 5) to reduce read-to-read jitter.
/// </remarks>
///
class QMC5883PMagnometer
{
public:
   ///
   /// <summary>
   /// Selects the sensor's full-scale measurement range (RNG field of CONTROL1). A smaller
   /// range gives finer resolution (more counts per Gauss) but saturates at a lower field
   /// strength, which matters when a magnet is held very close to the sensor. A larger range
   /// tolerates stronger nearby fields (e.g. a close magnet) at the cost of resolution.
   /// </summary>
   ///
   enum class Range : uint8_t
   {
      GAUSS_2 = 0x00, // +/-2 Gauss, most sensitive, saturates soonest
      GAUSS_8 = 0x01, // +/-8 Gauss, default - matches the sketch's original behavior
   };

private:
   static constexpr uint8_t ADDRESS = 0x2C;
   static constexpr uint8_t REG_DATA_X_LSB = 0x01;
   static constexpr uint8_t REG_CONTROL1 = 0x0A;
   static constexpr uint8_t REG_SET_RESET = 0x0B;
   static constexpr uint8_t CONTROL1_OSR_ODR_MODE = 0x1D; // OSR=0, ODR=200Hz, MODE=continuous (RNG bits added in begin())
   static constexpr float GAUSS_TO_MICROTESLA = 100.0f;

   Range _range;
   RollingAverage _x;
   RollingAverage _y;
   RollingAverage _z;

   ///
   /// <summary>
   /// Gets the counts-per-Gauss scale factor for the currently selected range.
   /// </summary>
   /// <returns>Counts per Gauss for the current range setting</returns>
   ///
   float countsPerGauss() const
   {
      return _range == Range::GAUSS_2 ? 12000.0f : 3000.0f;
   }

   ///
   /// <summary>
   /// Writes a single byte to a QMC5883P register.
   /// </summary>
   /// <param name="reg">Register address</param>
   /// <param name="value">Value to write</param>
   ///
   void writeRegister(uint8_t reg, uint8_t value)
   {
      Wire.beginTransmission(ADDRESS);
      Wire.write(reg);
      Wire.write(value);
      Wire.endTransmission();
   }

public:
   ///
   /// <summary>
   /// Initializes a QMC5883P driver.
   /// </summary>
   /// <param name="sampleCount">Number of samples averaged per axis.</param>
   /// <param name="range">Full-scale measurement range/sensitivity (see Range).</param>
   ///
   explicit QMC5883PMagnometer(size_t sampleCount = 5, Range range = Range::GAUSS_8)
      : _range(range),
        _x(sampleCount),
        _y(sampleCount),
        _z(sampleCount)
   {
   }

   ///
   /// <summary>
   /// Checks whether the sensor is present on the I2C bus and starts continuous measurement.
   /// </summary>
   /// <returns>True if the sensor ACK'd its I2C address; false otherwise.</returns>
   ///
   bool begin()
   {
      Wire.beginTransmission(ADDRESS);
      if (Wire.endTransmission() != 0)
      {
         return false;
      }

      writeRegister(REG_SET_RESET, 0x01);
      writeRegister(REG_CONTROL1, CONTROL1_OSR_ODR_MODE | (static_cast<uint8_t>(_range) << 4));

      return true;
   }

   ///
   /// <summary>
   /// Reads the latest magnetometer sample and adds it to each axis's rolling average. The
   /// sensor runs in continuous mode, so the data registers refresh on their own.
   /// </summary>
   ///
   void update()
   {
      Wire.beginTransmission(ADDRESS);
      Wire.write(REG_DATA_X_LSB);
      Wire.endTransmission();
      Wire.requestFrom(ADDRESS, (uint8_t)6);

      int16_t rawX = (int16_t)(Wire.read() | (Wire.read() << 8));
      int16_t rawY = (int16_t)(Wire.read() | (Wire.read() << 8));
      int16_t rawZ = (int16_t)(Wire.read() | (Wire.read() << 8));

      _x.set((rawX / countsPerGauss()) * GAUSS_TO_MICROTESLA);
      _y.set((rawY / countsPerGauss()) * GAUSS_TO_MICROTESLA);
      _z.set((rawZ / countsPerGauss()) * GAUSS_TO_MICROTESLA);
   }

   ///
   /// <summary>
   /// Gets the rolling-averaged X-axis field strength, in microtesla.
   /// </summary>
   /// <returns>X-axis field strength, in microtesla</returns>
   ///
   float x() const
   {
      return _x.get();
   }

   ///
   /// <summary>
   /// Gets the rolling-averaged Y-axis field strength, in microtesla.
   /// </summary>
   /// <returns>Y-axis field strength, in microtesla</returns>
   ///
   float y() const
   {
      return _y.get();
   }

   ///
   /// <summary>
   /// Gets the rolling-averaged Z-axis field strength, in microtesla.
   /// </summary>
   /// <returns>Z-axis field strength, in microtesla</returns>
   ///
   float z() const
   {
      return _z.get();
   }

   ///
   /// <summary>
   /// Computes the compass azimuth (0-360 degrees) from the rolling-averaged X and Y
   /// magnetic field components.
   /// </summary>
   /// <returns>Azimuth in degrees, 0-360</returns>
   ///
   float azimuth() const
   {
      float result = atan2(y(), x()) * 180.0f / (float)M_PI;
      if (result < 0.0f)
      {
         result += 360.0f;
      }

      return result;
   }
};
