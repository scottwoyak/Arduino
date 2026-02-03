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

