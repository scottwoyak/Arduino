#pragma once

#include <Adafruit_FRAM_SPI.h>

union ByteFloatUnion {
   uint8_t b[4];
   float f;
};

class FramSpiEx : public Adafruit_FRAM_SPI {
public:
   FramSpiEx(int8_t cs, SPIClass* theSPI = &SPI, uint32_t freq = 1000000) : Adafruit_FRAM_SPI(cs, theSPI, freq) {}
   FramSpiEx(int8_t clk, int8_t miso, int8_t mosi, int8_t cs) :Adafruit_FRAM_SPI(clk, miso, mosi, cs) {}

   float readFloat(uint32_t address) {
      ByteFloatUnion value;
      this->read(address, value.b, 4);
      return value.f;
   }

   void writeFloat(uint32_t address, float value) {
      ByteFloatUnion v;
      v.f = value;
      this->write(address, v.b, 4);
   }
};