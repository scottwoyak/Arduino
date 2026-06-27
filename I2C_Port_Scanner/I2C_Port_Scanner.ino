/// <summary>
/// I2C bus scanner for device discovery.
/// </summary>
/// <remarks>
/// Scans I2C addresses 1-126 and reports detected devices with their 7-bit hex addresses.
/// Runs continuously with 5-second intervals between scans. Adapted from the Arduino
/// I2C Scanner example. Useful for identifying connected I2C sensors and peripherals.
/// 
/// Based on: https://playground.arduino.cc/Main/I2cScanner/
/// Hardware: Arduino with I2C bus (SDA on pin 7, SCL on pin 8).
/// </remarks>

#include <Arduino.h>
#include <Wire.h>

constexpr uint8_t I2C_SDA = 7;
constexpr uint8_t I2C_SCL = 8;

constexpr uint8_t I2C_MIN_ADDR = 1;
constexpr uint8_t I2C_MAX_ADDR = 127;
constexpr unsigned long SCAN_INTERVAL_MS = 5000;

void setup()
{
   SerialX::begin();
   Wire.begin(I2C_SDA, I2C_SCL);

   Serial.println("\nI2C Port Scanner - Ready");
}

void loop()
{
   byte error, address;
   int nDevices = 0;

   Serial.println("Scanning I2C bus...");

   for (address = I2C_MIN_ADDR; address < I2C_MAX_ADDR; address++)
   {
      // Test if a device acknowledges at this address
      Wire.beginTransmission(address);
      error = Wire.endTransmission();

      if (error == 0)
      {
         // Device found
         Serial.print("I2C device found at address 0x");
         if (address < 16)
         {
            Serial.print("0");
         }
         Serial.print(address, HEX);
         Serial.println(" !");

         nDevices++;
      }
      else if (error == 4)
      {
         // Unknown error at this address
         Serial.print("Unknown error at address 0x");
         if (address < 16)
         {
            Serial.print("0");
         }
         Serial.println(address, HEX);
      }
   }

   if (nDevices == 0)
   {
      Serial.println("No I2C devices found\n");
   }
   else
   {
      Serial.println("Scan complete\n");
   }

   delay(SCAN_INTERVAL_MS);  // Wait before next scan
}
