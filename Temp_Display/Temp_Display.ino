//
// Temperature and humidity display for Feather boards.
//
// Continuously reads a connected sensor and shows the temperature and humidity, along
// with a table of sensor information and read rate.
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

#include "Table.h"
#include "DisplayValue.h"
#include "Rate.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "TempSensor.h"
#include "Util.h"

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
constexpr int16_t VALUE_PADDING_PX = 5;
Format tempFormat("###.##F");
Format humFormat("###.#%", Format::Alignment::RIGHT);
Format rateFormat("####/s", Format::Alignment::RIGHT);
constexpr const char* TYPE_ADDRESS_FORMAT = "################";
constexpr const char* ID_FORMAT = "################";
constexpr const char* CORRECTION_FORMAT = "+#.###F";
Table table(&arduino, 0, 0);
DisplayValue* rateField = nullptr;
int16_t headingHeight;

void setup()
{
   SerialX::begin();
   Util::checkTheLastShutdownReason();

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
      const SerialTable::Column columns[] = {
         { "Field", 10 },
         { "Value", 16 },
      };
      SerialTable serialTable("Temperature Sensor Detected", columns);
      serialTable.printHeader();
      serialTable.printRow("Type", sensor.type());
      serialTable.printRow("ID", sensor.id());
      serialTable.printRow("Address", "0x" + String(sensor.address(), HEX));
   }

   // Draw the heading, then reserve space below it for the large temp/humidity readout
   arduino.clearDisplay();
   arduino.setTextSize(HEADER_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("Temperature", Color::HEADING);
   headingHeight = arduino.charH();

   arduino.setTextSize(VALUE_SIZE);
   table.setPosition(0, headingHeight + VALUE_PADDING_PX + arduino.charH() + VALUE_PADDING_PX);
   table.addRow("Type", TYPE_ADDRESS_FORMAT, Color::LABEL, Color::VALUE2);
   table.addRow("ID", ID_FORMAT, Color::LABEL, Color::VALUE2);
   table.addRow("Correction", CORRECTION_FORMAT, Color::LABEL, Color::VALUE2);

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

   // Draw the large temp/humidity readout above the table
   arduino.setTextSize(VALUE_SIZE);
   arduino.setCursor(0, headingHeight + VALUE_PADDING_PX);
   arduino.print(temp, tempFormat, Color::VALUE);
   arduino.printlnR(hum, humFormat, Color::VALUE);

   table.setValue(0, String(sensor.type()) + " 0x" + String(sensor.address(), HEX), Color::VALUE2);
   table.setValue(1, sensor.id(), Color::VALUE2);
   if (sensor.hasTempCorrection())
   {
      table.setValue(2, sensor.tempCorrectionF(), Color::VALUE2);
   }
   else
   {
      table.setNoValue(2, Color::VALUE2);
   }

   rateField->draw(readRate.get(), Color::LIGHTGRAY);

   table.draw();
}
