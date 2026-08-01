#pragma once

#include <Arduino.h>

#if defined(_Adafruit_GRAYOLED_H_)

#define COLOR_MONOCHROME

///
/// <summary>
/// Display color values for monochrome displays. All non-black values map to the single
/// available foreground color; BLACK and DIMGRAY map to the background color.
/// </summary>
///
enum class Color : uint16_t
{
   // Full web/CSS color name set - on monochrome displays every color maps to the single
   // foreground color (1), except the handful that historically mapped to the background (0).
   ALICEBLUE = 1, ANTIQUEWHITE = 1, AQUA = 1, AQUAMARINE = 1, AZURE = 1, BEIGE = 1, BISQUE = 1,
   BLACK = 0, BLANCHEDALMOND = 1, BLUE = 1, BLUEVIOLET = 1, BROWN = 1, BURLYWOOD = 1,
   CADETBLUE = 1, CHARTREUSE = 1, CHOCOLATE = 1, CORAL = 1, CORNFLOWERBLUE = 1, CORNSILK = 1,
   CRIMSON = 1, CYAN = 1, DARKBLUE = 1, DARKCYAN = 1, DARKGOLDENROD = 1, DARKGRAY = 1,
   DARKGREEN = 1, DARKKHAKI = 1, DARKMAGENTA = 1, DARKOLIVEGREEN = 1, DARKORANGE = 1,
   DARKORCHID = 1, DARKRED = 1, DARKSALMON = 1, DARKSEAGREEN = 1, DARKSLATEBLUE = 1,
   DARKSLATEGRAY = 1, DARKTURQUOISE = 1, DARKVIOLET = 1, DEEPPINK = 1, DEEPSKYBLUE = 1,
   DIMGRAY = 0, DODGERBLUE = 1, FIREBRICK = 1, FLORALWHITE = 1, FORESTGREEN = 1, FUCHSIA = 1,
   GAINSBORO = 1, GHOSTWHITE = 1, GOLD = 1, GOLDENROD = 1, GRAY = 1, GREEN = 1,
   GREENYELLOW = 1, HONEYDEW = 1, HOTPINK = 1, INDIANRED = 1, INDIGO = 1, IVORY = 1,
   KHAKI = 1, LAVENDER = 1, LAVENDERBLUSH = 1, LAWNGREEN = 1, LEMONCHIFFON = 1,
   LIGHTBLUE = 1, LIGHTCORAL = 1, LIGHTCYAN = 1, LIGHTGOLDENRODYELLOW = 1, LIGHTGRAY = 1,
   LIGHTGREEN = 1, LIGHTPINK = 1, LIGHTSALMON = 1, LIGHTSEAGREEN = 1, LIGHTSKYBLUE = 1,
   LIGHTSLATEGRAY = 1, LIGHTSTEELBLUE = 1, LIGHTYELLOW = 1, LIME = 1, LIMEGREEN = 1,
   LINEN = 1, MAGENTA = 1, MAROON = 1, MEDIUMAQUAMARINE = 1, MEDIUMBLUE = 1,
   MEDIUMORCHID = 1, MEDIUMPURPLE = 1, MEDIUMSEAGREEN = 1, MEDIUMSLATEBLUE = 1,
   MEDIUMSPRINGGREEN = 1, MEDIUMTURQUOISE = 1, MEDIUMVIOLETRED = 1, MIDNIGHTBLUE = 1,
   MINTCREAM = 1, MISTYROSE = 1, MOCCASIN = 1, NAVAJOWHITE = 1, NAVY = 1, OLDLACE = 1,
   OLIVE = 1, OLIVEDRAB = 1, ORANGE = 1, ORANGERED = 1, ORCHID = 1, PALEGOLDENROD = 1,
   PALEGREEN = 1, PALETURQUOISE = 1, PALEVIOLETRED = 1, PAPAYAWHIP = 1, PEACHPUFF = 1,
   PERU = 1, PINK = 1, PLUM = 1, POWDERBLUE = 1, PURPLE = 1, REBECCAPURPLE = 1, RED = 1,
   ROSYBROWN = 1, ROYALBLUE = 1, SADDLEBROWN = 1, SALMON = 1, SANDYBROWN = 1, SEAGREEN = 1,
   SEASHELL = 1, SIENNA = 1, SILVER = 1, SKYBLUE = 1, SLATEBLUE = 1, SLATEGRAY = 1,
   SNOW = 1, SPRINGGREEN = 1, STEELBLUE = 1, TAN = 1, TEAL = 1, THISTLE = 1, TOMATO = 1,
   TURQUOISE = 1, VIOLET = 1, WHEAT = 1, WHITE = 1, WHITESMOKE = 1, YELLOW = 1,
   YELLOWGREEN = 1,

   HEADING = WHITE,
   HEADING2 = WHITE,
   SUB_HEADING = WHITE,
   LABEL = WHITE,
   VALUE = WHITE,
   VALUE2 = WHITE,
   VALUE3 = WHITE,
   SUB_LABEL = WHITE,
   SECTION_HEADER = WHITE,
   TABLE_HEADER = WHITE,
};

#else

#define COLOR_565

enum class Color : uint16_t;

namespace Color565
{
   ///
   /// <summary>
   /// Converts RGB color components to 565-bit color format.
   /// </summary>
   /// <param name="red">Red component (0-255)</param>
   /// <param name="green">Green component (0-255)</param>
   /// <param name="blue">Blue component (0-255)</param>
   /// <returns>Color in 565 format</returns>
   ///
   constexpr Color fromRGB(uint8_t red, uint8_t green, uint8_t blue)
   {
      return (Color) (((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
   }
}

///
/// <summary>
/// Display color values in RGB565 format, defined in terms of their RGB components via
/// Color565::fromRGB(). Names and values match the standard CSS/web color keywords.
/// </summary>
///
enum class Color : uint16_t
{
   ALICEBLUE = (uint16_t) Color565::fromRGB(240, 248, 255),
   ANTIQUEWHITE = (uint16_t) Color565::fromRGB(250, 235, 215),
   AQUA = (uint16_t) Color565::fromRGB(0, 255, 255),
   AQUAMARINE = (uint16_t) Color565::fromRGB(127, 255, 212),
   AZURE = (uint16_t) Color565::fromRGB(240, 255, 255),
   BEIGE = (uint16_t) Color565::fromRGB(245, 245, 220),
   BISQUE = (uint16_t) Color565::fromRGB(255, 228, 196),
   BLACK = (uint16_t) Color565::fromRGB(0, 0, 0),
   BLANCHEDALMOND = (uint16_t) Color565::fromRGB(255, 235, 205),
   BLUE = (uint16_t) Color565::fromRGB(0, 0, 255),
   BLUEVIOLET = (uint16_t) Color565::fromRGB(138, 43, 226),
   BROWN = (uint16_t) Color565::fromRGB(165, 42, 42),
   BURLYWOOD = (uint16_t) Color565::fromRGB(222, 184, 135),
   CADETBLUE = (uint16_t) Color565::fromRGB(95, 158, 160),
   CHARTREUSE = (uint16_t) Color565::fromRGB(127, 255, 0),
   CHOCOLATE = (uint16_t) Color565::fromRGB(210, 105, 30),
   CORAL = (uint16_t) Color565::fromRGB(255, 127, 80),
   CORNFLOWERBLUE = (uint16_t) Color565::fromRGB(100, 149, 237),
   CORNSILK = (uint16_t) Color565::fromRGB(255, 248, 220),
   CRIMSON = (uint16_t) Color565::fromRGB(220, 20, 60),
   CYAN = (uint16_t) Color565::fromRGB(0, 255, 255),
   DARKBLUE = (uint16_t) Color565::fromRGB(0, 0, 139),
   DARKCYAN = (uint16_t) Color565::fromRGB(0, 139, 139),
   DARKGOLDENROD = (uint16_t) Color565::fromRGB(184, 134, 11),
   DARKGRAY = (uint16_t) Color565::fromRGB(169, 169, 169),
   DARKGREEN = (uint16_t) Color565::fromRGB(0, 100, 0),
   DARKKHAKI = (uint16_t) Color565::fromRGB(189, 183, 107),
   DARKMAGENTA = (uint16_t) Color565::fromRGB(139, 0, 139),
   DARKOLIVEGREEN = (uint16_t) Color565::fromRGB(85, 107, 47),
   DARKORANGE = (uint16_t) Color565::fromRGB(255, 140, 0),
   DARKORCHID = (uint16_t) Color565::fromRGB(153, 50, 204),
   DARKRED = (uint16_t) Color565::fromRGB(139, 0, 0),
   DARKSALMON = (uint16_t) Color565::fromRGB(233, 150, 122),
   DARKSEAGREEN = (uint16_t) Color565::fromRGB(143, 188, 143),
   DARKSLATEBLUE = (uint16_t) Color565::fromRGB(72, 61, 139),
   DARKSLATEGRAY = (uint16_t) Color565::fromRGB(47, 79, 79),
   DARKTURQUOISE = (uint16_t) Color565::fromRGB(0, 206, 209),
   DARKVIOLET = (uint16_t) Color565::fromRGB(148, 0, 211),
   DEEPPINK = (uint16_t) Color565::fromRGB(255, 20, 147),
   DEEPSKYBLUE = (uint16_t) Color565::fromRGB(0, 191, 255),
   DIMGRAY = (uint16_t) Color565::fromRGB(105, 105, 105),
   DODGERBLUE = (uint16_t) Color565::fromRGB(30, 144, 255),
   FIREBRICK = (uint16_t) Color565::fromRGB(178, 34, 34),
   FLORALWHITE = (uint16_t) Color565::fromRGB(255, 250, 240),
   FORESTGREEN = (uint16_t) Color565::fromRGB(34, 139, 34),
   FUCHSIA = (uint16_t) Color565::fromRGB(255, 0, 255),
   GAINSBORO = (uint16_t) Color565::fromRGB(220, 220, 220),
   GHOSTWHITE = (uint16_t) Color565::fromRGB(248, 248, 255),
   GOLD = (uint16_t) Color565::fromRGB(255, 215, 0),
   GOLDENROD = (uint16_t) Color565::fromRGB(218, 165, 32),
   GRAY = (uint16_t) Color565::fromRGB(128, 128, 128),
   GREEN = (uint16_t) Color565::fromRGB(0, 128, 0),
   GREENYELLOW = (uint16_t) Color565::fromRGB(173, 255, 47),
   HONEYDEW = (uint16_t) Color565::fromRGB(240, 255, 240),
   HOTPINK = (uint16_t) Color565::fromRGB(255, 105, 180),
   INDIANRED = (uint16_t) Color565::fromRGB(205, 92, 92),
   INDIGO = (uint16_t) Color565::fromRGB(75, 0, 130),
   IVORY = (uint16_t) Color565::fromRGB(255, 255, 240),
   KHAKI = (uint16_t) Color565::fromRGB(240, 230, 140),
   LAVENDER = (uint16_t) Color565::fromRGB(230, 230, 250),
   LAVENDERBLUSH = (uint16_t) Color565::fromRGB(255, 240, 245),
   LAWNGREEN = (uint16_t) Color565::fromRGB(124, 252, 0),
   LEMONCHIFFON = (uint16_t) Color565::fromRGB(255, 250, 205),
   LIGHTBLUE = (uint16_t) Color565::fromRGB(173, 216, 230),
   LIGHTCORAL = (uint16_t) Color565::fromRGB(240, 128, 128),
   LIGHTCYAN = (uint16_t) Color565::fromRGB(224, 255, 255),
   LIGHTGOLDENRODYELLOW = (uint16_t) Color565::fromRGB(250, 250, 210),
   LIGHTGRAY = (uint16_t) Color565::fromRGB(211, 211, 211),
   LIGHTGREEN = (uint16_t) Color565::fromRGB(144, 238, 144),
   LIGHTPINK = (uint16_t) Color565::fromRGB(255, 182, 193),
   LIGHTSALMON = (uint16_t) Color565::fromRGB(255, 160, 122),
   LIGHTSEAGREEN = (uint16_t) Color565::fromRGB(32, 178, 170),
   LIGHTSKYBLUE = (uint16_t) Color565::fromRGB(135, 206, 250),
   LIGHTSLATEGRAY = (uint16_t) Color565::fromRGB(119, 136, 153),
   LIGHTSTEELBLUE = (uint16_t) Color565::fromRGB(176, 196, 222),
   LIGHTYELLOW = (uint16_t) Color565::fromRGB(255, 255, 224),
   LIME = (uint16_t) Color565::fromRGB(0, 255, 0),
   LIMEGREEN = (uint16_t) Color565::fromRGB(50, 205, 50),
   LINEN = (uint16_t) Color565::fromRGB(250, 240, 230),
   MAGENTA = (uint16_t) Color565::fromRGB(255, 0, 255),
   MAROON = (uint16_t) Color565::fromRGB(128, 0, 0),
   MEDIUMAQUAMARINE = (uint16_t) Color565::fromRGB(102, 205, 170),
   MEDIUMBLUE = (uint16_t) Color565::fromRGB(0, 0, 205),
   MEDIUMORCHID = (uint16_t) Color565::fromRGB(186, 85, 211),
   MEDIUMPURPLE = (uint16_t) Color565::fromRGB(147, 112, 219),
   MEDIUMSEAGREEN = (uint16_t) Color565::fromRGB(60, 179, 113),
   MEDIUMSLATEBLUE = (uint16_t) Color565::fromRGB(123, 104, 238),
   MEDIUMSPRINGGREEN = (uint16_t) Color565::fromRGB(0, 250, 154),
   MEDIUMTURQUOISE = (uint16_t) Color565::fromRGB(72, 209, 204),
   MEDIUMVIOLETRED = (uint16_t) Color565::fromRGB(199, 21, 133),
   MIDNIGHTBLUE = (uint16_t) Color565::fromRGB(25, 25, 112),
   MINTCREAM = (uint16_t) Color565::fromRGB(245, 255, 250),
   MISTYROSE = (uint16_t) Color565::fromRGB(255, 228, 225),
   MOCCASIN = (uint16_t) Color565::fromRGB(255, 228, 181),
   NAVAJOWHITE = (uint16_t) Color565::fromRGB(255, 222, 173),
   NAVY = (uint16_t) Color565::fromRGB(0, 0, 128),
   OLDLACE = (uint16_t) Color565::fromRGB(253, 245, 230),
   OLIVE = (uint16_t) Color565::fromRGB(128, 128, 0),
   OLIVEDRAB = (uint16_t) Color565::fromRGB(107, 142, 35),
   ORANGE = (uint16_t) Color565::fromRGB(255, 165, 0),
   ORANGERED = (uint16_t) Color565::fromRGB(255, 69, 0),
   ORCHID = (uint16_t) Color565::fromRGB(218, 112, 214),
   PALEGOLDENROD = (uint16_t) Color565::fromRGB(238, 232, 170),
   PALEGREEN = (uint16_t) Color565::fromRGB(152, 251, 152),
   PALETURQUOISE = (uint16_t) Color565::fromRGB(175, 238, 238),
   PALEVIOLETRED = (uint16_t) Color565::fromRGB(219, 112, 147),
   PAPAYAWHIP = (uint16_t) Color565::fromRGB(255, 239, 213),
   PEACHPUFF = (uint16_t) Color565::fromRGB(255, 218, 185),
   PERU = (uint16_t) Color565::fromRGB(205, 133, 63),
   PINK = (uint16_t) Color565::fromRGB(255, 192, 203),
   PLUM = (uint16_t) Color565::fromRGB(221, 160, 221),
   POWDERBLUE = (uint16_t) Color565::fromRGB(176, 224, 230),
   PURPLE = (uint16_t) Color565::fromRGB(128, 0, 128),
   REBECCAPURPLE = (uint16_t) Color565::fromRGB(102, 51, 153),
   RED = (uint16_t) Color565::fromRGB(255, 0, 0),
   ROSYBROWN = (uint16_t) Color565::fromRGB(188, 143, 143),
   ROYALBLUE = (uint16_t) Color565::fromRGB(65, 105, 225),
   SADDLEBROWN = (uint16_t) Color565::fromRGB(139, 69, 19),
   SALMON = (uint16_t) Color565::fromRGB(250, 128, 114),
   SANDYBROWN = (uint16_t) Color565::fromRGB(244, 164, 96),
   SEAGREEN = (uint16_t) Color565::fromRGB(46, 139, 87),
   SEASHELL = (uint16_t) Color565::fromRGB(255, 245, 238),
   SIENNA = (uint16_t) Color565::fromRGB(160, 82, 45),
   SILVER = (uint16_t) Color565::fromRGB(192, 192, 192),
   SKYBLUE = (uint16_t) Color565::fromRGB(135, 206, 235),
   SLATEBLUE = (uint16_t) Color565::fromRGB(106, 90, 205),
   SLATEGRAY = (uint16_t) Color565::fromRGB(112, 128, 144),
   SNOW = (uint16_t) Color565::fromRGB(255, 250, 250),
   SPRINGGREEN = (uint16_t) Color565::fromRGB(0, 255, 127),
   STEELBLUE = (uint16_t) Color565::fromRGB(70, 130, 180),
   TAN = (uint16_t) Color565::fromRGB(210, 180, 140),
   TEAL = (uint16_t) Color565::fromRGB(0, 128, 128),
   THISTLE = (uint16_t) Color565::fromRGB(216, 191, 216),
   TOMATO = (uint16_t) Color565::fromRGB(255, 99, 71),
   TURQUOISE = (uint16_t) Color565::fromRGB(64, 224, 208),
   VIOLET = (uint16_t) Color565::fromRGB(238, 130, 238),
   WHEAT = (uint16_t) Color565::fromRGB(245, 222, 179),
   WHITE = (uint16_t) Color565::fromRGB(255, 255, 255),
   WHITESMOKE = (uint16_t) Color565::fromRGB(245, 245, 245),
   YELLOW = (uint16_t) Color565::fromRGB(255, 255, 0),
   YELLOWGREEN = (uint16_t) Color565::fromRGB(154, 205, 50),

   // Semantic aliases, preserved at their historical visual values from before the full
   // web/CSS color set was added (some of these do not correspond exactly to any single
   // named web color, hence the literal fromRGB() calls).
   HEADING = (uint16_t) Color565::fromRGB(255, 128, 0),
   HEADING2 = CYAN,
   SUB_HEADING = (uint16_t) Color565::fromRGB(255, 204, 102),
   LABEL = WHITE,
   VALUE = YELLOW,
   VALUE2 = CYAN,
   VALUE3 = LIME,
   SUB_LABEL = (uint16_t) Color565::fromRGB(132, 132, 132),
   SECTION_HEADER = LIME,
   TABLE_HEADER = LIME,
};

#endif

namespace Color565
{
   ///
   /// <summary>
   /// Extracts red component (0-255) from 565-bit color.
   /// </summary>
   /// <param name="color">Color value (16-bit)</param>
   /// <returns>Red component (0-255)</returns>
   ///
   uint8_t getR(uint16_t color)
   {
      return (uint8_t) (255 * ((color >> 11) & 0x1F) / 31.0);
   }

   ///
   /// <summary>
   /// Extracts red component (0-255) from Color.
   /// </summary>
   /// <param name="color">Color value</param>
   /// <returns>Red component (0-255)</returns>
   ///
   uint8_t getR(Color color) { return getR((uint16_t)color); }

   ///
   /// <summary>
   /// Extracts green component (0-255) from 565-bit color.
   /// </summary>
   /// <param name="color">Color value (16-bit)</param>
   /// <returns>Green component (0-255)</returns>
   ///
   uint8_t getG(uint16_t color)
   {
      return (uint8_t) (255 * ((color >> 5) & 0x3F) / 63.0);
   }

   ///
   /// <summary>
   /// Extracts green component (0-255) from Color.
   /// </summary>
   /// <param name="color">Color value</param>
   /// <returns>Green component (0-255)</returns>
   ///
   uint8_t getG(Color color) { return getG((uint16_t)color); }

   ///
   /// <summary>
   /// Extracts blue component (0-255) from 565-bit color.
   /// </summary>
   /// <param name="color">Color value (16-bit)</param>
   /// <returns>Blue component (0-255)</returns>
   ///
   uint8_t getB(uint16_t color)
   {
      return (uint8_t) (255 * ((color & 0x1F) / 31.0));
   }

   ///
   /// <summary>
   /// Extracts blue component (0-255) from Color.
   /// </summary>
   /// <param name="color">Color value</param>
   /// <returns>Blue component (0-255)</returns>
   ///
   uint8_t getB(Color color) { return getB((uint16_t)color); }

   ///
   /// <summary>
   /// Blends two 565-bit colors using linear interpolation.
   /// </summary>
   /// <param name="c1">First color</param>
   /// <param name="c2">Second color</param>
   /// <param name="ratio">Blend ratio (0.0 = c1, 1.0 = c2)</param>
   /// <returns>Blended color</returns>
   ///
   Color blend(uint16_t c1, uint16_t c2, float ratio)
   {
      uint8_t r = constrain(getR(c1) + ratio * ((int16_t)getR(c2) - getR(c1)), 0, 255);
      uint8_t g = constrain(getG(c1) + ratio * ((int16_t)getG(c2) - getG(c1)), 0, 255);
      uint8_t b = constrain(getB(c1) + ratio * ((int16_t)getB(c2) - getB(c1)), 0, 255);
      return fromRGB(r, g, b);
   }

   ///
   /// <summary>
   /// Blends two Color values using linear interpolation.
   /// </summary>
   /// <param name="c1">First color</param>
   /// <param name="c2">Second color</param>
   /// <param name="ratio">Blend ratio (0.0 = c1, 1.0 = c2)</param>
   /// <returns>Blended color</returns>
   ///
   Color blend(Color c1, Color c2, float ratio) { return blend((uint16_t)c1, (uint16_t)c2, ratio); }

   ///
   /// <summary>
   /// Blends color values using linear interpolation (overload variant).
   /// </summary>
   /// <param name="c1">First color (16-bit)</param>
   /// <param name="c2">Second color</param>
   /// <param name="ratio">Blend ratio (0.0 = c1, 1.0 = c2)</param>
   /// <returns>Blended color</returns>
   ///
   Color blend(uint16_t c1, Color c2, float ratio) { return blend(c1, (uint16_t)c2, ratio); }

   ///
   /// <summary>
   /// Blends color values using linear interpolation (overload variant).
   /// </summary>
   /// <param name="c1">First color</param>
   /// <param name="c2">Second color (16-bit)</param>
   /// <param name="ratio">Blend ratio (0.0 = c1, 1.0 = c2)</param>
   /// <returns>Blended color</returns>
   ///
   Color blend(Color c1, uint16_t c2, float ratio) { return blend((uint16_t)c1, c2, ratio); }

   ///
   /// <summary>
   /// Prints the RGB components of a Color to Serial.
   /// </summary>
   /// <param name="color">Color to print</param>
   ///
   void print(Color color)
   {
      Serial.print("R:");
      Serial.print(getR(color));
      Serial.print(" G:");
      Serial.print(getG(color));
      Serial.print(" B:");
      Serial.print(getB(color));
   }

   ///
   /// <summary>
   /// Prints the RGB components of a 565-bit color to Serial.
   /// </summary>
   /// <param name="color">Color value (16-bit)</param>
   ///
   void print(uint16_t color) { print((Color)color); }

   ///
   /// <summary>
   /// Prints the RGB components of a Color to Serial, followed by a newline.
   /// </summary>
   /// <param name="color">Color to print</param>
   ///
   void println(Color color)
   {
      print(color);
      Serial.println();
   }

   ///
   /// <summary>
   /// Prints the RGB components of a 565-bit color to Serial, followed by a newline.
   /// </summary>
   /// <param name="color">Color value (16-bit)</param>
   ///
   void println(uint16_t color) { println((Color)color); }
}

