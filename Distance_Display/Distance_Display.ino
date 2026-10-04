//
// Distance display for Feather boards.
//
// Continuously reads a VL53L1X time-of-flight distance sensor and shows the distance
// in millimeters, centered on the display, along with the measurement rate.
//
// Hardware: Feather display board with a VL53L1X sensor on I2C.
//
// Requires the Adafruit_VL53L1X library (install it into the libraries folder).
//

#include <Arduino.h>
#include <Wire.h>

#include <Adafruit_VL53L1X.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#include "WrongBoard.h"
#endif

#include "DisplayValue.h"
#include "Rate.h"
#include "SerialX.h"

// ----------- The Board
Arduino arduino;

// ----------- Text Sizes
constexpr uint8_t HEADER_SIZE = 3;
constexpr uint8_t VALUE_SIZE = 4;
constexpr uint8_t FOOTER_SIZE = 2;

// ----------- Sensor
Adafruit_VL53L1X sensor;
bool sensorFound = false;
Rate readRate;

// ----------- Display Items
Format distFormat("#### mm", Format::Alignment::CENTER);
Format rateFormat("######/s", Format::Alignment::RIGHT);
DisplayValue* rateField = nullptr;
int16_t valueStartY;

void setup()
{
   SerialX::begin();

   Wire.begin();
   arduino.begin();

   sensorFound = sensor.begin() && sensor.startRanging();
   if (!sensorFound)
   {
      Serial.println("Error: No VL53L1X distance sensor detected");
   }

   // Draw the heading, then center the distance readout in the space between it and the footer
   arduino.clearDisplay();
   arduino.setTextSize(HEADER_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("Distance", Color::HEADING);
   int16_t headingHeight = arduino.charH();
   int16_t footerHeight = arduino.charH(FOOTER_SIZE);
   int16_t valueHeight = arduino.charH(VALUE_SIZE);
   int16_t availableHeight = arduino.height() - headingHeight - footerHeight;
   valueStartY = headingHeight + (availableHeight - valueHeight) / 2;

   // Rate is shown in the lower right corner
   arduino.setTextSize(FOOTER_SIZE);
   rateField = new DisplayValue(&arduino, rateFormat, FOOTER_SIZE, DisplayValue::Alignment::RIGHT);
   rateField->setPosition(arduino.width(), arduino.height() - arduino.charH());

   // Sensor name is shown in the lower left corner
   arduino.setCursor(0, arduino.height() - arduino.charH());
   arduino.print("VL53L1X", Color::LIGHTGRAY);

   readRate.start();
}

void loop()
{
   if (!sensorFound || !sensor.dataReady())
   {
      return;
   }

   int16_t distance = sensor.distance();
   sensor.clearInterrupt();

   // The rate is the time between new measurements
   readRate.stop();
   readRate.start();

   // distance() returns -1 when the measurement failed (e.g. nothing in range)
   bool valid = distance >= 0;

   arduino.setTextSize(VALUE_SIZE);
   arduino.setCursorY(valueStartY);
   arduino.printlnC(valid ? (float)distance : 0.0f, distFormat, valid ? Color::VALUE : Color::GRAY);

   arduino.setTextSize(FOOTER_SIZE);
   rateField->draw(readRate.get(), Color::LIGHTGRAY);
}
