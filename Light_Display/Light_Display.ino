//
// Light level display for Feather boards.
//
// Continuously reads a VEML7700 ambient light sensor and shows the light level in lux,
// centered on the display, along with the read rate.
//
// Hardware: Feather display board with a VEML7700 sensor connected via I2C.
// Requires the Adafruit VEML7700 library.
//

#include <Arduino.h>
#include <Wire.h>

#include <Adafruit_VEML7700.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
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
Adafruit_VEML7700 sensor;
Rate readRate;

// ----------- Display Items
Format luxFormat("######.# lx", Format::Alignment::LEFT);
Format rateFormat("######/s", Format::Alignment::RIGHT);
DisplayValue* rateField = nullptr;
int16_t valueStartY;
bool sensorFound = false;

void setup()
{
   SerialX::begin();

   Wire.begin();
   arduino.begin();

   sensorFound = sensor.begin();
   if (!sensorFound)
   {
      Serial.println("Error: No VEML7700 sensor detected");
   }
   else
   {
      Serial.println("VEML7700 Sensor Detected");
   }

   // Draw the heading, then center the lux readout in the space between it and the footer
   arduino.clearDisplay();
   arduino.setTextSize(HEADER_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("Light", Color::HEADING);
   int16_t headingHeight = arduino.charH();
   int16_t footerHeight = arduino.charH(FOOTER_SIZE);
   int16_t valueHeight = arduino.charH(VALUE_SIZE);
   int16_t availableHeight = arduino.height() - headingHeight - footerHeight;
   valueStartY = headingHeight + (availableHeight - valueHeight) / 2;

   // Rate is shown in the lower right corner
   arduino.setTextSize(FOOTER_SIZE);
   rateField = new DisplayValue(&arduino, rateFormat, FOOTER_SIZE, DisplayValue::Alignment::RIGHT);
   rateField->setPosition(arduino.width(), arduino.height() - arduino.charH());
}

void loop()
{
   readRate.start();
   float lux = sensorFound ? sensor.readLux() : 0;
   readRate.stop();

   arduino.setTextSize(VALUE_SIZE);
   arduino.setCursorY(valueStartY);
   arduino.printlnC(lux, luxFormat, sensorFound ? Color::VALUE : Color::GRAY);

   // Sensor name is shown in the lower left corner
   arduino.setTextSize(FOOTER_SIZE);
   arduino.setCursor(0, arduino.height() - arduino.charH());
   arduino.print("VEML7700", Color::LIGHTGRAY);

   rateField->draw(readRate.get(), Color::LIGHTGRAY);
}
