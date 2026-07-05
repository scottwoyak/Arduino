/// <summary>
/// Temperature and humidity display for Feather boards.
/// </summary>
/// <remarks>
/// Continuously reads a connected sensor and shows temperature, humidity, read duration,
/// and read rate on the display.
///
/// Define ONE_WIRE_PIN to use a DS18B20 sensor; otherwise an I2C temperature/humidity
/// sensor is auto-detected. Hardware: Feather display board with supported sensor.
/// </remarks>

#include <Arduino.h>
#include <Wire.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "Rate.h"
#include "SerialX.h"
#include "TempSensor.h"

// Uncomment to use DS18B20 sensor instead of I2C auto-detection
// #define ONE_WIRE_PIN 5

Arduino arduino;
TempSensor sensor;

Format tempFormat("###.## F");
Format humFormat("###.#%");
Format rateFormat("####/s");
Format msFormat("####.# ms");

Rate tempRate;  // Timer for temperature read performance
Rate humRate;   // Timer for humidity read performance

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
      Serial.println("Temperature Sensor Detected:");
      Serial.println("  Type: " + String(sensor.type()));
      Serial.println("  ID: " + String(sensor.id()));
      Serial.println("  Address: 0x" + String(sensor.address(), HEX));
   }
}

void loop()
{
   arduino.setCursor(0, 0);

   // Read temperature with timing
   tempRate.start();
   float temp = sensor.readTemperatureF();
   tempRate.stop();
   float tempTime = tempRate.elapsedMicros() / 1000.0f;

   // Read humidity with timing
   humRate.start();
   float hum = sensor.readHumidity();
   humRate.stop();
   float humTime = humRate.elapsedMicros() / 1000.0f;

   // Display temperature value
   arduino.setTextSize(3);
   if (arduino.display.width() / arduino.charW() > 12)
   {
      arduino.print("Temp: ", Color::LABEL);
   }
   arduino.println(temp, tempFormat, Color::VALUE);

   // Display temperature read metrics
   arduino.setTextSize(2);
   arduino.print(tempTime, msFormat, Color::SUB_LABEL);
   arduino.print("  ");
   arduino.print(tempRate.get(), rateFormat, Color::SUB_LABEL);
   arduino.println();
   arduino.moveCursorY(arduino.charH() / 2);

   // Display humidity value
   arduino.setTextSize(3);
   if (arduino.display.width() / arduino.charW() > 12)
   {
      arduino.print(" Hum: ", Color::LABEL);
   }
   arduino.println(hum, humFormat, Color::VALUE);

   // Display humidity read metrics
   arduino.setTextSize(2);
   arduino.print(humTime, msFormat, Color::SUB_LABEL);
   arduino.print("  ");
   arduino.print(humRate.get(), rateFormat, Color::SUB_LABEL);
   arduino.println();

   // Display sensor information in corner
   arduino.setTextSize(2);
   arduino.setCursor(0, -2 * arduino.charH() + 1);
   arduino.print("Type: ", sensor.type(), Color::VALUE2);
   arduino.print(" 0x", Color::VALUE2);
   arduino.println(sensor.address(), HEX, Color::VALUE2);

   arduino.moveCursorY(1);
   arduino.println("  ID: ", sensor.id(), Color::VALUE2);
}
