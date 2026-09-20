#pragma once

#if defined(ADAFRUIT_FEATHER_M0)

#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_BUILTIN_LED_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

// Distinct identifier for this physical board/wiring variant, used to board-qualify
// OTA/publish asset names (see OTAUpdater::_BOARD_ID). Kept separate from ARDUINO_BOARD
// since multiple wiring variants can share the same underlying Arduino IDE board type.
#define ARDUINO_BOARD_VARIANT_ID "ADAFRUIT_FEATHER_M0"

#include "Feather_M0_OLED.h"
using Arduino = Feather_M0_OLED;

constexpr uint8_t DEFAULT_HEADING_SIZE = 2;
constexpr uint8_t DEFAULT_TEXT_SIZE = 1;
constexpr uint8_t DEFAULT_CONTENT_SIZE = 2;

#elif defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)

#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_BUILTIN_LED_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "ADAFRUIT_FEATHER_ESP32S3_TFT"

#include "Feather_ESP32_S3.h"
using Arduino = Feather_ESP32_S3;

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;
constexpr uint8_t DEFAULT_CONTENT_SIZE = 2;

#elif defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS)
// Waveshare ESP32-S3-Zero wired with a custom-powered I2C bus and an RGB LED status
// indicator (see WaveShare_ESP32_S3_Zero_Sensors constructor for required pins).

#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32_S3_ZERO_SENSORS"

#include "Waveshare_ESP32_S3_Zero.h"
using Arduino = WaveShare_ESP32_S3_Zero_Sensors;

// This board has no onboard display, so no text-size defaults are defined.

#elif defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO)

#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32_S3_ZERO"

#include "Waveshare_ESP32_S3_Zero.h"
using Arduino = WaveShare_ESP32_S3_Zero;

// This board has no onboard display, so no text-size defaults are defined.

#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_43)

#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_TOUCH_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32S3_TOUCH_LCD_43"

#include "Waveshare_ESP32S3_Touch_LCD_43.h"
using Arduino = Waveshare_ESP32S3_Touch_LCD_43;

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;
constexpr uint8_t DEFAULT_CONTENT_SIZE = 2;

#elif defined(ARDUINO_HOSYOND_ESP32_S3_VIEWER)
// Hosyond ESP32-S3 dev board (builtin 4" ST7796S display with FT6336U capacitive
// touch, BOOT button as buttonA). Selected via manual #define since it shares the
// "ESP32S3 Dev Module" Arduino IDE board type with ARDUINO_ESP32S3_DEV (Playground).

#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_TOUCH_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "HOSYOND_ESP32_S3_VIEWER"

#include "ViewerBoardS3.h"
using Arduino = ViewerBoardS3;

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;
constexpr uint8_t DEFAULT_CONTENT_SIZE = 3;

#elif defined(ARDUINO_ESP32S3_DEV)
// Generic ESP32S3 Dev Module boards are assumed to be wired up as a Playground setup
// (LGX_ST7796 display, two rotary encoders, two standalone buttons).

#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_BUILTIN_LED_SUPPORTED
#define ARDUINO_PLAYGROUND_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "ESP32S3_DEV_PLAYGROUND"

#include "ESP32_S3_Playground.h"
using Arduino = ESP32_S3_Playground;

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;
constexpr uint8_t DEFAULT_CONTENT_SIZE = 3;

#else
#warning "No Arduino Board Type Defined"
#endif
