#pragma once

#if defined(ADAFRUIT_FEATHER_M0)

#include "Feather_M0_OLED.h"
typedef Feather_M0_OLED Feather;

#elif defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)

#include "Feather_ESP32_S3.h"
typedef Feather_ESP32_S3 Feather;

#endif
