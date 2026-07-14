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

#elif defined(ARDUINO_ESP32S3_DEV)
// Generic ESP32S3 Dev Module boards are assumed to be wired up as a Playground setup
// (LGX_ST7796 display, two rotary encoders, two standalone buttons).

#include "ESP32_S3_Playground.h"
using Arduino = ESP32_S3_Playground;

#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_PLAYGROUND_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

#endif
