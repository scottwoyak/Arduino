#pragma once

// Board feature macros and ARDUINO_BOARD_VARIANT_ID only. This header intentionally has no
// includes so that low-level headers (e.g. DeviceServerClient.h) can use the macros without
// pulling in the board classes (and ArduinoBase.h), which would create an include cycle.
// See ArduinoBoard.h for the board class includes and the Arduino alias.

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

#elif defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)

#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_BUILTIN_LED_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "ADAFRUIT_FEATHER_ESP32S3_TFT"

#elif defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS)
// Waveshare ESP32-S3-Zero wired with a custom-powered I2C bus and an RGB LED status
// indicator (see WaveShare_ESP32_S3_Zero_Sensors constructor for required pins).

#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32_S3_ZERO_SENSORS"

#elif defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO)

#define ARDUINO_NEOPIXEL_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32_S3_ZERO"

#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_43)

#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_TOUCH_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "WAVESHARE_ESP32S3_TOUCH_LCD_43"

#elif defined(ARDUINO_HOSYOND_ESP32_S3_VIEWER)
// Hosyond ESP32-S3 dev board (builtin 4" ST7796S display with FT6336U capacitive
// touch, BOOT button as buttonA). Selected via manual #define since it shares the
// "ESP32S3 Dev Module" Arduino IDE board type with ARDUINO_ESP32S3_DEV (Playground).

#define ARDUINO_BUTTON_SUPPORTED
#define ARDUINO_BUTTON_A_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_TOUCH_SUPPORTED
#define ARDUINO_PREFERENCES_SUPPORTED
#define ARDUINO_STATUS_SUPPORTED

#define ARDUINO_BOARD_VARIANT_ID "HOSYOND_ESP32_S3_VIEWER"

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

#else
#warning "No Arduino Board Type Defined"
#endif
