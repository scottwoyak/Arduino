#pragma once

#include <Arduino.h>
#include <Wire.h>

///
/// <summary>
/// Driver for the AS5600 magnetic rotary position sensor. Unlike a magnetometer (which
/// senses Earth's weak, uniform field), this chip is a Hall-effect angle sensor built to
/// sense the direction of a strong diametrically-magnetized magnet mounted directly on
/// the rotation axis, centered above the die. It outputs a 12-bit angle (0-4095, mapping
/// to 0-360 degrees) directly, with no atan2 math required by the caller.
/// </summary>
/// <remarks>
/// Call read() before reading rawAngle() or angleDegrees(); those accessors just return
/// the value from the last read() call.
/// </remarks>
///
class AS5600
{
private:
   static constexpr uint8_t ADDRESS = 0x36;
   static constexpr uint8_t REG_STATUS = 0x0B;
   static constexpr uint8_t REG_ANGLE_H = 0x0E;
   static constexpr uint8_t STATUS_MAGNET_DETECTED_BIT = 0x20;
   static constexpr uint16_t COUNTS_PER_REVOLUTION = 4096;

   uint16_t _rawAngle = 0;

   ///
   /// <summary>
   /// Reads a 16-bit big-endian value starting at the given register.
   /// </summary>
   /// <param name="reg">Register address of the high byte</param>
   /// <returns>The 16-bit value read from the sensor</returns>
   ///
   uint16_t _readRegister16(uint8_t reg) const
   {
      Wire.beginTransmission(ADDRESS);
      Wire.write(reg);
      Wire.endTransmission();
      Wire.requestFrom(ADDRESS, (uint8_t)2);

      uint8_t highByte = Wire.read();
      uint8_t lowByte = Wire.read();
      return (uint16_t)((highByte << 8) | lowByte);
   }

public:
   ///
   /// <summary>
   /// Checks whether the sensor is present on the I2C bus and detects a magnet in range.
   /// </summary>
   /// <returns>True if the sensor ACK'd its I2C address and detects a magnet; false otherwise.</returns>
   ///
   bool begin()
   {
      Wire.beginTransmission(ADDRESS);
      if (Wire.endTransmission() != 0)
      {
         return false;
      }

      Wire.beginTransmission(ADDRESS);
      Wire.write(REG_STATUS);
      Wire.endTransmission();
      Wire.requestFrom(ADDRESS, (uint8_t)1);
      uint8_t status = Wire.read();

      return (status & STATUS_MAGNET_DETECTED_BIT) != 0;
   }

   ///
   /// <summary>
   /// Reads the latest angle sample from the sensor.
   /// </summary>
   ///
   void read()
   {
      _rawAngle = _readRegister16(REG_ANGLE_H) & 0x0FFF;
   }

   ///
   /// <summary>
   /// Gets the raw 12-bit angle count (0-4095) from the last read() call.
   /// </summary>
   /// <returns>Raw angle count, 0-4095</returns>
   ///
   uint16_t rawAngle() const
   {
      return _rawAngle;
   }

   ///
   /// <summary>
   /// Gets the angle in degrees (0-360) from the last read() call.
   /// </summary>
   /// <returns>Angle in degrees, 0-360</returns>
   ///
   float angleDegrees() const
   {
      return (_rawAngle * 360.0f) / COUNTS_PER_REVOLUTION;
   }
};
