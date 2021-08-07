#pragma once

#include <SPI.h>
#include <Wire.h>

class SPIMultiplexor {
private:
   uint8_t _address;

public:
   SPIMultiplexor(uint8_t address = 0x70) {
      this->_address = address;
   }

   void begin() {
      Wire.begin();
   }

   void scan() {
      Serial.println("Scanning Ports for SPI addresses");
      for (uint8_t port = 0; port < 8; port++) {
         this->select(port);
         Serial.print("Port: ");
         Serial.println(port);

         for (uint8_t addr = 0; addr <= 127; addr++) {
            if (addr == this->_address) continue; // us - the multiplexor

            Wire.beginTransmission(addr);
            if (!Wire.endTransmission()) {
               Serial.print("    0x");
               Serial.println(addr, HEX);
            }
         }
      }
   }

   void select(uint8_t i) {
      if (i > 7) return;

      Wire.beginTransmission(this->_address);
      Wire.write(1 << i);
      Wire.endTransmission();
   }
};