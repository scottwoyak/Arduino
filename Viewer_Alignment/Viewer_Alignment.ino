//
// Sketch for the Hosyond ESP32-S3 4" display (Viewer board), also usable on the
// Waveshare ESP32-S3-Touch-LCD-4.3B.
//
// Draws a white 1-pixel border around the display and a 2-pixel wide crosshair
// through the center point, for checking display alignment/calibration.
//

// Uncomment this to build for the Waveshare ESP32-S3-Touch-LCD-4.3B instead of the
// selected Visual Micro board (that board has no dedicated board package entry).
//#define ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_43

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Viewer)."
#include "WrongBoard.h"
#endif

constexpr uint8_t CROSSHAIR_THICKNESS = 2;

Arduino arduino;

void setup()
{
   arduino.begin();

   uint16_t w = arduino.width();
   uint16_t h = arduino.height();
   Point16 center = arduino.center();

   arduino.display.drawRect(0, 0, w, h, TFT_WHITE);

   arduino.display.fillRect(center.x - CROSSHAIR_THICKNESS / 2, 0, CROSSHAIR_THICKNESS, h, TFT_WHITE);
   arduino.display.fillRect(0, center.y - CROSSHAIR_THICKNESS / 2, w, CROSSHAIR_THICKNESS, TFT_WHITE);
}

void loop()
{
}
