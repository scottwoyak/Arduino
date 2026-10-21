#pragma once

#include <stdint.h>

// Board feature macros and ARDUINO_BOARD_VARIANT_ID only. This header intentionally has no
// includes so that low-level headers (e.g. DeviceServerClient.h) can use the macros without
// pulling in the board classes (and ArduinoBase.h), which would create an include cycle.
// See ArduinoBoard.h for the board class includes and the Arduino alias.

#if defined(ADAFRUIT_FEATHER_M0)

// #define ARDUINO_BATTERY_SUPPORTED
#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_BUILTIN_LED_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

// Distinct identifier for this physical board/wiring variant, used to board-qualify
// OTA/publish asset names (see OTAUpdater::_BOARD_ID). Kept separate from ARDUINO_BOARD
// since multiple wiring variants can share the same underlying Arduino IDE board type.
#define ARDUINO_BOARD_VARIANT_ID "ADAFRUIT_FEATHER_M0"

constexpr uint8_t DEFAULT_HEADING_SIZE = 2;
constexpr uint8_t DEFAULT_TEXT_SIZE = 1;

#elif defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)

#define ARDUINO_BATTERY_SUPPORTED
#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_BUILTIN_LED_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "ADAFRUIT_FEATHER_ESP32S3_TFT"

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;

#elif defined(ARDUINO_WAVESHARE_ESP32S3_ZERO_SENSORS) || defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS)
// Waveshare ESP32-S3-Zero wired with a custom-powered I2C bus and an RGB LED status
// indicator (see WaveShare_ESP32_S3_Zero_Sensors constructor for required pins).

#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32S3_ZERO_SENSORS"

// This board has no onboard display, so no text-size defaults are defined.

#elif defined(ARDUINO_WAVESHARE_ESP32S3_ZERO_SENSORS2) || defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS2)
// Waveshare ESP32-S3-Zero wired with a custom-powered I2C bus and a 2-LED NeoPixel strip
// for status (pixel 0) and general-purpose LED (pixel 1).

#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32S3_ZERO_SENSORS2"

// This board has no onboard display, so no text-size defaults are defined.

#elif defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO)

#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32_S3_ZERO"

// This board has no onboard display, so no text-size defaults are defined.

#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_43)

// #define ARDUINO_BATTERY_SUPPORTED
#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_TOUCH_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32S3_TOUCH_LCD_43"

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;

#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_19)

// #define ARDUINO_BATTERY_SUPPORTED
#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_TOUCH_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32S3_TOUCH_LCD_19"

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;

#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_20) || defined(ARDUINO_WAVESHARE_ESP32_S3_TOUCH_LCD_20)

// #define ARDUINO_BATTERY_SUPPORTED
#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_TOUCH_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32S3_TOUCH_LCD_20"

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;

#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_28) || defined(ARDUINO_WAVESHARE_ESP32_S3_TOUCH_LCD_28)

// #define ARDUINO_BATTERY_SUPPORTED
#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_TOUCH_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32S3_TOUCH_LCD_28"

constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;

#elif defined(ARDUINO_HOSYOND_ESP32S3_TOUCH_LCD_40) || defined(ARDUINO_HOSYOND_ESP32_S3_VIEWER)
// Hosyond 4.0 ESP32-S3-Touch-LCD board (builtin 4" ST7796S display with FT6336U
// capacitive touch, ES8311 audio codec, BOOT button as buttonA).

// #define ARDUINO_BATTERY_SUPPORTED
#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_TOUCH_SUPPORTED
#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED
#define ARDUINO_SOUND_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "HOSYOND_ESP32S3_TOUCH_LCD_40"

constexpr uint8_t DEFAULT_HEADING_SIZE = 4;
constexpr uint8_t DEFAULT_TEXT_SIZE = 3;

#elif defined(ARDUINO_ESP32S3_DEV_PLAYGROUND)
// ESP32-S3 Dev Module wired up as a Playground setup
// (LGX_ST7796 display, two rotary encoders, two standalone buttons).

#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_BUILTIN_LED_SUPPORTED
#define ARDUINO_PLAYGROUND_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "ESP32S3_DEV_PLAYGROUND"

constexpr uint8_t DEFAULT_HEADING_SIZE = 4;
constexpr uint8_t DEFAULT_TEXT_SIZE = 3;

#else
#warning "No Arduino Board Type Defined"
constexpr uint8_t DEFAULT_HEADING_SIZE = 3;
constexpr uint8_t DEFAULT_TEXT_SIZE = 2;
#endif
