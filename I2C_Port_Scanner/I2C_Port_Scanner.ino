//
// I2C bus scanner for device discovery.
//
// Scans addresses 1-126 and reports detected devices with their 7-bit hex addresses.
// Runs continuously with 5-second intervals between scans. Useful for identifying
// connected I2C sensors and peripherals.
//
// Based on: https://playground.arduino.cc/Main/I2cScanner/
//

#include <Arduino.h>
#include <Wire.h>
#include "SerialX.h"

constexpr uint8_t I2C_SDA = 7;
constexpr uint8_t I2C_SCL = 8;

constexpr uint8_t I2C_MIN_ADDR = 1;  // First valid 7-bit I2C address to probe
constexpr uint8_t I2C_MAX_ADDR = 127;  // One past the last 7-bit I2C address to probe
constexpr unsigned long SCAN_INTERVAL_MS = 5000;

void setup()
{
   SerialX::begin();
   Wire.begin(I2C_SDA, I2C_SCL);

   Serial.println("\nI2C Port Scanner - Ready");
}

void loop()
{
   uint8_t error = 0;
   int nDevices = 0;

   Serial.println("Scanning I2C bus...");

   for (uint8_t address = I2C_MIN_ADDR; address < I2C_MAX_ADDR; address++)
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
