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

#include "Feather.h"
#include "Rate.h"
#include "SerialX.h"
#include "TempSensor.h"

// Uncomment to use DS18B20 sensor instead of I2C auto-detection
// #define ONE_WIRE_PIN 5

Feather feather;
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
   feather.begin();

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
   feather.setCursor(0, 0);

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
   feather.setTextSize(3);
   if (feather.display.width() / feather.charW() > 12)
   {
      feather.print("Temp: ", Color::LABEL);
   }
   feather.println(temp, tempFormat, Color::VALUE);

   // Display temperature read metrics
   feather.setTextSize(2);
   feather.print(tempTime, msFormat, Color::SUB_LABEL);
   feather.print("  ");
   feather.print(tempRate.get(), rateFormat, Color::SUB_LABEL);
   feather.println();
   feather.moveCursorY(feather.charH() / 2);

   // Display humidity value
   feather.setTextSize(3);
   if (feather.display.width() / feather.charW() > 12)
   {
      feather.print(" Hum: ", Color::LABEL);
   }
   feather.println(hum, humFormat, Color::VALUE);

   // Display humidity read metrics
   feather.setTextSize(2);
   feather.print(humTime, msFormat, Color::SUB_LABEL);
   feather.print("  ");
   feather.print(humRate.get(), rateFormat, Color::SUB_LABEL);
   feather.println();

   // Display sensor information in corner
   feather.setTextSize(2);
   feather.setCursor(0, -2 * feather.charH() + 1);
   feather.print("Type: ", sensor.type(), Color::VALUE2);
   feather.print(" 0x", Color::VALUE2);
   feather.println(sensor.address(), HEX, Color::VALUE2);

   feather.moveCursorY(1);
   feather.println("  ID: ", sensor.id(), Color::VALUE2);
}
