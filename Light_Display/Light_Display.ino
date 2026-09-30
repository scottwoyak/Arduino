//
// Light level display for Feather boards.
//
// Continuously reads an ambient light sensor and shows the light level in lux,
// centered on the display, along with the read rate.
//
// Hardware: Feather display board with a VEML7700 or BH1750 (GY-302) sensor on I2C.
// The sensor type is auto-detected from its I2C address.
//

#include <Arduino.h>
#include <Wire.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "DisplayValue.h"
#include "LightSensor.h"
#include "Rate.h"
#include "SerialX.h"

// ----------- The Board
Arduino arduino;

// ----------- Text Sizes
constexpr uint8_t HEADER_SIZE = 3;
constexpr uint8_t VALUE_SIZE = 4;
constexpr uint8_t FOOTER_SIZE = 2;

// ----------- Sensor
LightSensor sensor;
Rate readRate;

// ----------- Display Items
Format luxFormat("#####.# lux", Format::Alignment::CENTER);
Format rateFormat("######/s", Format::Alignment::RIGHT);
DisplayValue* rateField = nullptr;
int16_t valueStartY;
float lastLux = NAN;

void setup()
{
   SerialX::begin();

   Wire.begin();
   arduino.begin();

   sensor.begin();
   if (!sensor.exists())
   {
      Serial.println("Error: No light sensor detected");
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

   readRate.start();
}

void loop()
{
   float lux = sensor.exists() ? sensor.readLux() : 0;
   if (lux == lastLux)
   {
      return;
   }
   lastLux = lux;

   // The rate is the time between changed values
   readRate.stop();
   readRate.start();

   arduino.setTextSize(VALUE_SIZE);
   arduino.setCursorY(valueStartY);
   arduino.printlnC(lux, luxFormat, sensor.exists() ? Color::VALUE : Color::GRAY);

   // Sensor name is shown in the lower left corner
   arduino.setTextSize(FOOTER_SIZE);
   arduino.setCursor(0, arduino.height() - arduino.charH());
   arduino.print(sensor.type(), Color::LIGHTGRAY);

   rateField->draw(readRate.get(), Color::LIGHTGRAY);
}
