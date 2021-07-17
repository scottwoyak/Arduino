#pragma once

#include <Adafruit_FRAM_SPI.h>

union ByteFloatUnion {
   uint8_t bytes[4];
   float value;
};

union ByteIntUnion {
   uint8_t bytes[2];
   int16_t value;
};

class FramSpiEx : public Adafruit_FRAM_SPI {
public:
   FramSpiEx(int8_t cs, SPIClass* theSPI = &SPI, uint32_t freq = 1000000) : Adafruit_FRAM_SPI(cs, theSPI, freq) {}
   FramSpiEx(int8_t clk, int8_t miso, int8_t mosi, int8_t cs) :Adafruit_FRAM_SPI(clk, miso, mosi, cs) {}

   float readFloat(uint32_t address) {
      ByteFloatUnion u;
      this->read(address, u.bytes, 4);
      return u.value;
   }

   void writeFloat(uint32_t address, float value) {
      ByteFloatUnion u;
      u.value = value;
      this->write(address, u.bytes, 4);
   }

   int16_t readInt(uint32_t address) {
      ByteIntUnion u;
      this->read(address, u.bytes, 2);
      return u.value;
   }

   void writeInt(uint32_t address, int16_t value) {
      ByteIntUnion u;
      u.value = value;
      this->write(address, u.bytes, 2);
   }
};