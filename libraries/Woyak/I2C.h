#pragma once

#include <SPI.h>
#include <Wire.h>

class I2C {
public:
   static void scan() {
      Serial.println("Scanning for I2C addresses");
      for (uint8_t addr = 0; addr <= 127; addr++) {
         Wire.beginTransmission(addr);
         if (Wire.endTransmission() == 0) {
            Serial.print("    0x");
            Serial.println(addr, HEX);
         }
      }
   }

   static bool exists(uint8_t addr) {
      Wire.beginTransmission(addr);
      return (Wire.endTransmission() == 0);
   }
};