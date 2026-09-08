#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90393.h>

///
/// <summary>
/// Driver wrapper for the MLX90393 3-axis hall effect sensor, presenting the same
/// begin()/read()/x()/y()/z()/azimuth() surface as QMC5883PMagnometer so it can be used
/// as a drop-in replacement in azimuth-based sketches.
/// </summary>
/// <remarks>
/// Call read() before reading x(), y(), z(), or azimuth() to fetch the sensor's latest
/// sample; those accessors just return the values from the last read() call.
/// </remarks>
///
class MLX90393Magnetometer
{
private:
   Adafruit_MLX90393 _sensor;
   float _x = 0.0f;
   float _y = 0.0f;
   float _z = 0.0f;

public:
   ///
   /// <summary>
   /// Checks whether the sensor is present on the I2C bus and configures its gain,
   /// resolution, oversampling, and filter settings.
   /// </summary>
   /// <returns>True if the sensor was found and configured; false otherwise.</returns>
   ///
   bool begin()
   {
      if (!_sensor.begin_I2C())
      {
         return false;
      }

      _sensor.setGain(MLX90393_GAIN_1X);
      _sensor.setResolution(MLX90393_X, MLX90393_RES_16);
      _sensor.setResolution(MLX90393_Y, MLX90393_RES_16);
      _sensor.setResolution(MLX90393_Z, MLX90393_RES_16);
      // OSR_0/FILTER_6 keeps the conversion time (13.36 ms, per the driver's tconv
      // table) plus its fixed 10 ms delay low enough for a ~30 Hz sample rate, using
      // the strongest filtering that still fits.
      _sensor.setOversampling(MLX90393_OSR_0);
      _sensor.setFilter(MLX90393_FILTER_6);

      return true;
   }

   ///
   /// <summary>
   /// Reads the latest magnetometer sample from the sensor. If the read fails (e.g. the
   /// first read immediately after begin(), before the sensor's initial conversion has
   /// completed), the previous sample is retained instead of being overwritten with
   /// invalid data.
   /// </summary>
   ///
   void read()
   {
      float x;
      float y;
      float z;
      if (_sensor.readData(&x, &y, &z))
      {
         _x = x;
         _y = y;
         _z = z;
      }
   }

   ///
   /// <summary>
   /// Gets the X-axis field strength from the last read() call, in microtesla.
   /// </summary>
   /// <returns>X-axis field strength, in microtesla</returns>
   ///
   float x() const
   {
      return _x;
   }

   ///
   /// <summary>
   /// Gets the Y-axis field strength from the last read() call, in microtesla.
   /// </summary>
   /// <returns>Y-axis field strength, in microtesla</returns>
   ///
   float y() const
   {
      return _y;
   }

   ///
   /// <summary>
   /// Gets the Z-axis field strength from the last read() call, in microtesla.
   /// </summary>
   /// <returns>Z-axis field strength, in microtesla</returns>
   ///
   float z() const
   {
      return _z;
   }

   ///
   /// <summary>
   /// Computes the compass azimuth (0-360 degrees) from the X and Y magnetic field
   /// components read by the last read() call.
   /// </summary>
   /// <returns>Azimuth in degrees, 0-360</returns>
   ///
   float azimuth() const
   {
      float result = atan2(-_y, _x) * 180.0f / (float)M_PI;
      if (result < 0.0f)
      {
         result += 360.0f;
      }

      return result;
   }
};
