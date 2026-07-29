//
// Temperature and humidity display for Feather boards.
//
// Continuously reads a connected sensor and shows the temperature and humidity, centered
// on the display, along with the sensor's type/address and read rate.
//
// Define ONE_WIRE_PIN to use a DS18B20 sensor; otherwise an I2C temperature/humidity
// sensor is auto-detected. Hardware: Feather display board with supported sensor.
//

#include <Arduino.h>
#include <Wire.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "DisplayValue.h"
#include "Rate.h"
#include "SerialX.h"
#include "TempSensor.h"

// Uncomment to use DS18B20 sensor instead of I2C auto-detection
// #define ONE_WIRE_PIN 5

// ----------- The Board
Arduino arduino;

// ----------- Text Sizes
constexpr uint8_t HEADER_SIZE = 3;
constexpr uint8_t VALUE_SIZE = 4;
constexpr uint8_t FOOTER_SIZE = 2;

// ----------- Sensor
TempSensor sensor;
Rate readRate;  // Timer for combined temperature/humidity read performance

// ----------- Display Items
Format tempFormat("###.##F", Format::Alignment::LEFT);
Format humFormat("###.#% ", Format::Alignment::LEFT);
Format rateFormat("####/s", Format::Alignment::RIGHT);
DisplayValue* rateField = nullptr;
int16_t valueStartY;

void setup()
{
   SerialX::begin();

   Wire.begin();
   arduino.begin();

   // Initialize temperature sensor
   #ifdef ONE_WIRE_PIN
      sensor.begin(ONE_WIRE_PIN, true);
   #else
      sensor.begin();  // Auto-detect I2C sensor
   #endif

   // Log sensor info
   if (!sensor.exists())
   {
      Serial.println("Error: No temperature sensor detected");
   }
   else
   {
      Serial.println("Temperature Sensor Detected");
      Serial.print("Type: ");
      Serial.println(sensor.type());
      Serial.print("ID: ");
      Serial.println(sensor.id());
      Serial.print("Address: 0x");
      Serial.println(sensor.address(), HEX);
   }

   // Draw the heading, then compute the vertical space remaining below it and above the
   // footer row so the temp/humidity readout can be centered within that space.
   arduino.clearDisplay();
   arduino.setTextSize(HEADER_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("Temperature", Color::HEADING);
   int16_t headingHeight = arduino.charH();
   int16_t footerHeight = arduino.charH(FOOTER_SIZE);
   int16_t valuesHeight = 2 * arduino.charH(VALUE_SIZE);
   int16_t availableHeight = arduino.height() - headingHeight - footerHeight;
   valueStartY = headingHeight + (availableHeight - valuesHeight) / 2;

   // Rate is shown separately in the lower right corner, in gray
   arduino.setTextSize(FOOTER_SIZE);
   rateField = new DisplayValue(&arduino, rateFormat, FOOTER_SIZE, DisplayValue::Alignment::RIGHT);
   rateField->setPosition(arduino.width(), arduino.height() - arduino.charH());
}

void loop()
{
   // Read temperature and humidity with timing
   readRate.start();
   float temp;
   float hum;
   sensor.readBoth(temp, hum);
   readRate.stop();

   // Draw the temperature and humidity stacked on top of each other, vertically centered
   // in the space below the heading and horizontally centered on the display. If the
   // sensor doesn't support humidity, show a grayed-out placeholder instead.
   arduino.setTextSize(VALUE_SIZE);
   arduino.setCursorY(valueStartY);
   arduino.printlnC(temp, tempFormat, Color::VALUE);
   if (sensor.supportsHumidity())
   {
      arduino.printlnC(hum, humFormat, Color::VALUE);
   }
   else
   {
      arduino.printlnC(humFormat, Color::GRAY);
   }

   // Sensor type and address are shown in the lower left corner
   arduino.setTextSize(FOOTER_SIZE);
   arduino.setCursor(0, arduino.height() - arduino.charH());
   arduino.print(String(sensor.type()) + " 0x" + String(sensor.address(), HEX), Color::LIGHTGRAY);

   rateField->draw(readRate.get(), Color::LIGHTGRAY);
}
