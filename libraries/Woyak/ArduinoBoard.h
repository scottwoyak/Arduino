#pragma once

#if defined(ADAFRUIT_FEATHER_M0)

#include "Feather_M0_OLED.h"
using Arduino = Feather_M0_OLED;

#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

#elif defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)

#include "Feather_ESP32_S3.h"
using Arduino = Feather_ESP32_S3;

#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_LED_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

#elif defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO)

#include "Waveshare_ESP32_S3_Zero.h"
using Arduino = Waveshare_ESP32_S3_Zero;

#define ARDUINO_LED_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

#endif
