#pragma once

#include "ArduinoBoardId.h"

#if defined(ADAFRUIT_FEATHER_M0)

#include "Feather_M0_OLED.h"
using Arduino = Feather_M0_OLED;

constexpr uint8_t DEFAULT_HEADING_SIZE = 2;
constexpr uint8_t DEFAULT_TEXT_SIZE = 1;
constexpr uint8_t DEFAULT_CONTENT_SIZE = 2;

#elif defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)

#include "Feather_ESP32_S3.h"
using Arduino = Feather_ESP32_S3;

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;
constexpr uint8_t DEFAULT_CONTENT_SIZE = 2;

#elif defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS)

#include "Waveshare_ESP32_S3_Zero.h"
using Arduino = WaveShare_ESP32_S3_Zero_Sensors;

// This board has no onboard display, so no text-size defaults are defined.

#elif defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO)

#include "Waveshare_ESP32_S3_Zero.h"
using Arduino = WaveShare_ESP32_S3_Zero;

// This board has no onboard display, so no text-size defaults are defined.

#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_43)

#include "Waveshare_ESP32S3_Touch_LCD_43.h"
using Arduino = Waveshare_ESP32S3_Touch_LCD_43;

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;
constexpr uint8_t DEFAULT_CONTENT_SIZE = 2;

#elif defined(ARDUINO_HOSYOND_ESP32_S3_VIEWER)

#include "ViewerBoardS3.h"
using Arduino = ViewerBoardS3;

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;
constexpr uint8_t DEFAULT_CONTENT_SIZE = 3;

#elif defined(ARDUINO_ESP32S3_DEV)

#include "ESP32_S3_Playground.h"
using Arduino = ESP32_S3_Playground;

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;
constexpr uint8_t DEFAULT_CONTENT_SIZE = 3;

#endif
