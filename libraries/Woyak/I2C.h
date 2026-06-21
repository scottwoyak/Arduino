#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace I2C
{
   /// <summary>
   /// Scans and prints all responding I2C device addresses.
   /// </summary>
   inline void scan()
   {
      Serial.println("Scanning for I2C addresses");
      for (uint8_t addr = 0; addr <= 0x7F; addr++)
      {
         Wire.beginTransmission(addr);
         if (Wire.endTransmission() == 0)
         {
            Serial.print("    0x");
            Serial.println(addr, HEX);
         }
      }
   }

   /// <summary>
   /// Checks whether an I2C device responds at the specified address.
   /// </summary>
   /// <param name="addr">The 7-bit I2C device address to probe.</param>
   /// <returns>True when a device acknowledges the address; otherwise false.</returns>
   inline bool exists(uint8_t addr)
   {
      Wire.beginTransmission(addr);
      return Wire.endTransmission() == 0;
   }
}