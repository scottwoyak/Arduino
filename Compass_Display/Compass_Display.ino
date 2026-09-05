//
// Compass display using a QMC5883P magnetometer (as found on HW-127/GY-273 breakouts).
//
// Continuously reads the sensor and displays a header along with a table on the left
// showing the raw X, Y, Z magnetic field components and the computed azimuth (compass
// heading in degrees, 0-360).
//

#include <Arduino.h>
#include <Wire.h>
#include <cmath>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "DisplayBuffer.h"
#include "QMC5883PMagnometer.h"
#include "SerialX.h"
#include "Util.h"

// ----------- The Board
Arduino arduino;

// ----------- Text Sizes
constexpr uint8_t HEADER_SIZE = 3;
constexpr uint8_t TABLE_SIZE = 2;
constexpr uint8_t AZIMUTH_SIZE = 4;
constexpr uint8_t HEADER_GAP_PX = 10;

// ----------- Sensor
QMC5883PMagnometer sensor;

// ----------- Display Items
Format axisFormat("+####.# uT");
Format azimuthFormat("###.#");

// ----------- Azimuth Needle Buffer
constexpr uint8_t NEEDLE_LAYER = 1;
DisplayBuffer needleBuffer;

void setup()
{
   SerialX::begin();

   Wire.begin();
   arduino.begin();

   if (!sensor.begin())
   {
      Serial.println("QMC5883P Not Found (no I2C ACK - check wiring)");
      Util::reset(10);
   }

   arduino.clearDisplay();
   arduino.setTextSize(HEADER_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("Compass", Color::HEADING);

   int16_t needleRegionLeft = (int16_t)arduino.width() / 2;
   needleBuffer.bind(&arduino, needleRegionLeft, 0, (int16_t)arduino.width() - needleRegionLeft, (int16_t)arduino.height());
   needleBuffer.setPaletteColor(NEEDLE_LAYER, Color::WHITE);
}

void loop()
{
   sensor.read();

   arduino.setTextSize(TABLE_SIZE);
   arduino.setCursor(0, arduino.charH(HEADER_SIZE) + HEADER_GAP_PX);

   arduino.print("X: ", Color::LABEL);
   arduino.println(sensor.x(), axisFormat, Color::VALUE);

   arduino.print("Y: ", Color::LABEL);
   arduino.println(sensor.y(), axisFormat, Color::VALUE);

   arduino.print("Z: ", Color::LABEL);
   arduino.println(sensor.z(), axisFormat, Color::VALUE);

   arduino.setTextSize(AZIMUTH_SIZE);
   arduino.println(sensor.azimuth(), azimuthFormat, Color::VALUE2);

   int16_t centerY = (int16_t)arduino.height() / 2;
   int16_t needleRegionLeft = (int16_t)arduino.width() / 2;
   int16_t startX = (int16_t)arduino.width() - (int16_t)arduino.width() / 4 - needleRegionLeft;
   int16_t lineLength = (int16_t)arduino.width() / 4;

   float azimuthRad = sensor.azimuth() * (float)M_PI / 180.0f;
   int16_t endX = startX + (int16_t)(lineLength * sin(azimuthRad));
   int16_t endY = centerY - (int16_t)(lineLength * cos(azimuthRad));

   needleBuffer.clear();
   needleBuffer.drawLine(startX, centerY, endX, endY, NEEDLE_LAYER);
   needleBuffer.draw();
}
