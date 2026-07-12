#pragma once

#include <string>

#if defined(_Adafruit_GRAYOLED_H_)

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
   PINK = 1,
   GRAY = 1,
   DARKGRAY = 0,
   LIGHTGRAY = 1,

   HEADING = WHITE,
   HEADING2 = WHITE,
   LABEL = WHITE,
   VALUE = WHITE,
   VALUE2 = WHITE,
   VALUE3 = WHITE,
   SUB_LABEL = WHITE,
};

#else

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
   LIGHTGRAY = 0xC618,
   PINK = 0xFD9C,

   HEADING = ORANGE,
   HEADING2 = CYAN,
   LABEL = WHITE,
   VALUE = YELLOW,
   VALUE2 = CYAN,
   VALUE3 = GREEN,
   SUB_LABEL = GRAY,
};

#endif

namespace Color565
{
   /// <summary>
   /// Converts RGB color components to 565-bit color format.
   /// </summary>
   /// <param name="red">Red component (0-255)</param>
   /// <param name="green">Green component (0-255)</param>
   /// <param name="blue">Blue component (0-255)</param>
   /// <returns>Color in 565 format</returns>
   Color fromRGB(uint8_t red, uint8_t green, uint8_t blue)
   {
      return (Color) (((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
   }

   /// <summary>Extracts red component (0-255) from 565-bit color.</summary>
   /// <param name="color">Color value (16-bit)</param>
   /// <returns>Red component (0-255)</returns>
   uint8_t getR(uint16_t color) 
   { 
      return (uint8_t) (255*((color >> 11) & 0x1F)/31.0); 
   }

   /// <summary>Extracts red component (0-255) from Color.</summary>
   uint8_t getR(Color color) { return getR((uint16_t)color); }

   /// <summary>Extracts green component (0-255) from 565-bit color.</summary>
   /// <param name="color">Color value (16-bit)</param>
   /// <returns>Green component (0-255)</returns>
   uint8_t getG(uint16_t color) 
   { 
      return (uint8_t) (255*((color >> 5) & 0x3F)/63.0); 
   }

   /// <summary>Extracts green component (0-255) from Color.</summary>
   uint8_t getG(Color color) { return getG((uint16_t)color); }

   /// <summary>Extracts blue component (0-255) from 565-bit color.</summary>
   /// <param name="color">Color value (16-bit)</param>
   /// <returns>Blue component (0-255)</returns>
   uint8_t getB(uint16_t color) 
   { 
      return (uint8_t)(255 * ((color & 0x1F) / 31.0));
   }

   /// <summary>Extracts blue component (0-255) from Color.</summary>
   uint8_t getB(Color color) { return getB((uint16_t)color); }

   /// <summary>
   /// Blends two 565-bit colors using linear interpolation.
   /// </summary>
   /// <param name="c1">First color</param>
   /// <param name="c2">Second color</param>
   /// <param name="ratio">Blend ratio (0.0=c1, 1.0=c2)</param>
   /// <returns>Blended color</returns>
   Color blend(uint16_t c1, uint16_t c2, float ratio)
   {
      uint8_t r = constrain(getR(c1) + ratio * ((int16_t)getR(c2) - getR(c1)), 0, 255);
      uint8_t g = constrain(getG(c1) + ratio * ((int16_t)getG(c2) - getG(c1)), 0, 255);
      uint8_t b = constrain(getB(c1) + ratio * ((int16_t)getB(c2) - getB(c1)), 0, 255);
      return fromRGB(r, g, b);
   }

   /// <summary>Blends two Color values using linear interpolation.</summary>
   /// <param name="c1">First color</param>
   /// <param name="c2">Second color</param>
   /// <param name="ratio">Blend ratio (0.0=c1, 1.0=c2)</param>
   /// <returns>Blended color</returns>
   Color blend(Color c1, Color c2, float ratio) { return blend((uint16_t)c1, (uint16_t)c2, ratio); }

   /// <summary>Blends color values using linear interpolation (overload variants).</summary>
   Color blend(uint16_t c1, Color c2, float ratio) { return blend((uint16_t)c1, (uint16_t)c2, ratio); }

   /// <summary>Blends color values using linear interpolation (overload variants).</summary>
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


