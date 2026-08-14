//
// Temperature and humidity serial logger.
//
// Continuously reads a connected sensor and prints the temperature and humidity to
// Serial once per second. No display output.
//
// Define ONE_WIRE_PIN to use a DS18B20 sensor; otherwise an I2C temperature/humidity
// sensor is auto-detected. Hardware: any board with a supported sensor (no display
// required).
//

#include <Arduino.h>

// On the Waveshare ESP32-S3-Zero, use the sensors board wrapper for its custom I2C pins.
#if defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO)
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS
#endif

#include "ArduinoBoard.h"
#include "SerialX.h"
#include "TempSensor.h"
#include "Timer.h"

// Uncomment to use DS18B20 sensor instead of I2C auto-detection
// #define ONE_WIRE_PIN 5

constexpr uint16_t PRINT_INTERVAL_S = 3;

// ----------- The Board
Arduino arduino;

// ----------- Sensor
TempSensor sensor;
Timer printTimer(1000*PRINT_INTERVAL_S);

void setup()
{
   SerialX::begin();

   // The board wrapper's begin() handles any needed I2C bus setup.
   arduino.begin();

#ifdef ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS
   Serial.print("I2C SDA pin: ");
   Serial.println(arduino.sdaPin());
   Serial.print("I2C SCL pin: ");
   Serial.println(arduino.sclPin());
   Serial.print("I2C aux ground pin: ");
   Serial.println(arduino.i2cAuxGroundPin());
   Serial.print("I2C aux power pin: ");
   Serial.println(arduino.i2cAuxPowerPin());
#else
   // Boards without custom I2C wiring use the default SDA/SCL pins baked into the
   // board's variant definition.
   Serial.print("I2C SDA pin: ");
   Serial.println(SDA);
   Serial.print("I2C SCL pin: ");
   Serial.println(SCL);
#endif

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
}

void loop()
{
   if (printTimer.ready())
   {
      float temp;
      float hum;
      sensor.readBoth(temp, hum);

      Serial.print("Temp: ");
      Serial.print(temp);
      Serial.print(" F");

      if (sensor.supportsHumidity())
      {
         Serial.print("  Humidity: ");
         Serial.print(hum);
         Serial.print(" %");
      }

      Serial.println();
   }
}
