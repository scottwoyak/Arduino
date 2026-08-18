#pragma once

#include <Wire.h>
#include "I2C.h"

class I2CMultiplexor
{
private:
   uint8_t _address;

public:
   explicit I2CMultiplexor(uint8_t address = 0x70)
   {
      _address = address;
   }

   void scan()
   {
      Serial.println("Scanning Ports for I2C addresses");
      for (uint8_t port = 0; port < 8; port++)
      {
         select(port);
         Serial.print("Port: ");
         Serial.println(port);

         for (uint8_t addr = 0; addr <= 127; addr++)
         {
            if (addr == _address)
            {
               continue; // us - the multiplexor
            }

            if (I2C::exists(addr))
            {
               Serial.print("    0x");
               Serial.println(addr, HEX);
            }
         }
      }
   }

   void select(uint8_t i)
   {
      if (i > 7)
      {
         return;
      }

      Wire.beginTransmission(_address);
      Wire.write(1 << i);
      Wire.endTransmission();
   }

   ///
   /// <summary>
   /// Deselects all multiplexor channels, isolating every downstream port. Needed
   /// before talking to a sensor wired directly to the main I2C bus, so a previously
   /// selected downstream sensor (which may share the same address) doesn't also
   /// respond and conflict on the bus.
   /// </summary>
   ///
   void off()
   {
      Wire.beginTransmission(_address);
      Wire.write(0);
      Wire.endTransmission();
   }
};
