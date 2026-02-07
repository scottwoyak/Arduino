#pragma once

#include <Adafruit_GFX.h> 
#include <Format.h>
#include <string>

// TODO add more defines for more displays and move to a separate file
#if defined(_ADAFRUIT_ST77XXH_)
#define COLOR_565

enum class Color : uint16_t
{
   BLACK = 0x0000,
   WHITE = 0xFFFF,
   RED = 0xF800,
   GREEN = 0x07E0,
   BLUE = 0x001F,
   CYAN = 0x07FF,
   MAGENTA = 0xF81F,
   YELLOW = 0xFFE0,
   ORANGE = 0xFC00,
   GRAY = 0x8430,
   DARKGRAY = 0x6B4D,
   PINK = 0xFD9C,

   HEADING = ORANGE,
   HEADING2 = CYAN,
   LABEL = WHITE,
   VALUE = YELLOW,
   VALUE2 = CYAN,
   SUB_LABEL = GRAY,
};

#elif defined(_Adafruit_GRAYOLED_H_)
#define COLOR_MONOCHROME

enum class Color : uint16_t
{
   WHITE = 1,
   BLACK = 0,
   RED = 1,
   GREEN = 1,
   BLUE = 1,
   CYAN = 1,
   MAGENTA = 1,
   YELLOW = 1,
   ORANGE = 1,
   GRAY = 1,
   DARKGRAY = 0,

   HEADING = WHITE,
   HEADING2 = WHITE,
   LABEL = WHITE,
   VALUE = WHITE,
   VALUE2 = WHITE,
   SUB_LABEL = WHITE,
};

#endif

namespace Color565
{
   Color fromRGB(uint8_t red, uint8_t green, uint8_t blue)
   {
      return (Color) (((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
   }

   uint8_t getR(uint16_t color) 
   { 
      return (uint8_t) (255*((color >> 11) & 0x1F)/31.0); 
   }
   uint8_t getR(Color color) { return getR((uint16_t)color); }

   uint8_t getG(uint16_t color) 
   { 
      return (uint8_t) (255*((color >> 5) & 0x3F)/63.0); 
   }
   uint8_t getG(Color color) { return getG((uint16_t)color); }

   uint8_t getB(uint16_t color) 
   { 
      return (uint8_t)(255 * ((color & 0x1F) / 31.0));
   }
   uint8_t getB(Color color) { return getB((uint16_t)color); }

   Color blend(uint16_t c1, uint16_t c2, float ratio)
   {
      uint8_t r = constrain(getR(c1) + ratio * ((int16_t)getR(c2) - getR(c1)), 0, 255);
      uint8_t g = constrain(getG(c1) + ratio * ((int16_t)getG(c2) - getG(c1)), 0, 255);
      uint8_t b = constrain(getB(c1) + ratio * ((int16_t)getB(c2) - getB(c1)), 0, 255);
      return fromRGB(r, g, b);
   }


   Color blend(Color c1, Color c2, float ratio) { return blend((uint16_t)c1, (uint16_t)c2, ratio); }
   Color blend(uint16_t c1, Color c2, float ratio) { return blend((uint16_t)c1, (uint16_t)c2, ratio); }
   Color blend(Color c1, uint16_t c2, float ratio) { return blend((uint16_t)c1, (uint16_t)c2, ratio); }

   void print(Color color)
   {
      Serial.print("R:");
      Serial.print(getR(color));
      Serial.print(" G:");
      Serial.print(getG(color));
      Serial.print(" B:");
      Serial.print(getB(color));
   }
   void print(uint16_t color) { print((Color)color); }

   void println(Color color)
   {
      print(color);
      Serial.println();
   }
   void println(uint16_t color) { println((Color)color); }
}


