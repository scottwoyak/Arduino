#pragma once

#include "ArduinoBoardId.h"

#if defined(ADAFRUIT_FEATHER_M0)

#include "Feather_M0_OLED.h"
using Arduino = Feather_M0_OLED;

#elif defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)

#include "Feather_ESP32_S3.h"
using Arduino = Feather_ESP32_S3;

#elif defined(ARDUINO_WAVESHARE_ESP32S3_ZERO_SENSORS) || defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS)

#include "Waveshare_ESP32_S3_Zero.h"
using Arduino = WaveShare_ESP32_S3_Zero_Sensors;

#elif defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO)

#include "Waveshare_ESP32_S3_Zero.h"
using Arduino = WaveShare_ESP32_S3_Zero;

#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_43)

#include "Waveshare_ESP32S3_Touch_LCD_43.h"
using Arduino = Waveshare_ESP32S3_Touch_LCD_43;

#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_19)

#include "Waveshare_ESP32S3_Touch_LCD_19.h"
using Arduino = Waveshare_ESP32S3_Touch_LCD_19;

#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_20) || defined(ARDUINO_WAVESHARE_ESP32_S3_TOUCH_LCD_20)

#include "Waveshare_ESP32S3_Touch_LCD_20.h"
using Arduino = Waveshare_ESP32S3_Touch_LCD_20;

#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_28) || defined(ARDUINO_WAVESHARE_ESP32_S3_TOUCH_LCD_28)

#include "Waveshare_ESP32S3_Touch_LCD_28.h"
using Arduino = Waveshare_ESP32S3_Touch_LCD_28;

#elif defined(ARDUINO_HOSYOND_ESP32S3_TOUCH_LCD_40) || defined(ARDUINO_HOSYOND_ESP32_S3_VIEWER)

#include "ViewerBoardS3.h"
using Arduino = ViewerBoardS3;

#elif defined(ARDUINO_ESP32S3_DEV_PLAYGROUND)

#include "ESP32_S3_Playground.h"
using Arduino = ESP32_S3_Playground;

#endif
