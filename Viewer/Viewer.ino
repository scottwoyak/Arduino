//
// Sketch for the Hosyond ESP32-S3 4" display (Viewer board), also usable on the
// Waveshare ESP32-S3-Touch-LCD-4.3B.
//

// Uncomment this to build for the Waveshare ESP32-S3-Touch-LCD-4.3B instead of the
// Hosyond ESP32-S3 Viewer board. Also requires selecting the generic "ESP32S3 Dev
// Module" board in Visual Micro (that board has no dedicated board package entry).
//#define ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_43

// Default: build for the Hosyond ESP32-S3 Viewer board. Also requires selecting the
// generic "ESP32S3 Dev Module" board in Visual Micro (that board has no dedicated
// board package entry).
#define ARDUINO_HOSYOND_ESP32_S3_VIEWER

#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Viewer)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Viewer)."
#endif

#include "Field.h"
#include "RollingRate.h"

Arduino arduino;
RollingRate fps(100);

constexpr uint8_t COUNTER_TEXT_SIZE = 5;
constexpr uint8_t BUTTON_TEXT_SIZE = 3;
constexpr uint8_t FPS_TEXT_SIZE = 3;

Field counterField(&arduino, Point16(0, 0), "########", COUNTER_TEXT_SIZE);
Field buttonField(&arduino, Point16(0, 0), "ButtonA:", Format(5), BUTTON_TEXT_SIZE, Field::Alignment::LEFT);
Field fpsField(&arduino, Point16(0, 0), "###.# fps", FPS_TEXT_SIZE);

void setup()
{
   arduino.begin();

   int16_t buttonY = arduino.charH(COUNTER_TEXT_SIZE) + arduino.charH(COUNTER_TEXT_SIZE) / 4;
   counterField.setPosition(Point16(0, 0));
   buttonField.setPosition(Point16(0, buttonY));
   fpsField.setPosition(Point16(0, -arduino.charH(FPS_TEXT_SIZE)));
}

long counter = 0;

void loop()
{
   fps.tick();

   counterField.draw(counter++);
   buttonField.draw(arduino.buttonA.isPressed() ? "TRUE" : "FALSE");
   fpsField.draw(fps.get());
}
