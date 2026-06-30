//
// I2C bus scanner with multiplexor port support.
//
// Scans all 8 ports of a TCA9548A I2C multiplexor (default address 0x70) and reports
// devices detected on each port. Useful for systems using I2C multiplexing to connect
// multiple sensors to the same I2C bus. Automatically detects Waveshare ESP32-S3 Zero
// hardware and configures I2C accordingly. Rescans every 5 seconds.
//

#include <Arduino.h>
#include <Wire.h>
#include "SerialX.h"

constexpr uint8_t I2C_SDA = 7;
constexpr uint8_t I2C_SCL = 8;

constexpr uint8_t MULTIPLEXOR_ADDR = 0x70;  // Default TCA9548A address
constexpr uint8_t NUM_MULTIPLEXOR_PORTS = 8;

constexpr uint8_t I2C_MIN_ADDR = 1;
constexpr uint8_t I2C_MAX_ADDR = 127;
constexpr unsigned long SCAN_INTERVAL_MS = 5000;

void setup()
{
   SerialX::begin();

#if defined ARDUINO_WAVESHARE_ESP32_S3_ZERO
   #pragma message "Detected Waveshare ESP32-S3-Zero"
   Wire.begin(I2C_SDA, I2C_SCL);
#else
   Wire.begin();
#endif

   // Test for multiplexor presence
   Wire.beginTransmission(MULTIPLEXOR_ADDR);
   if (Wire.endTransmission() != 0)
   {
      Serial.println("ERROR: I2C multiplexor not found at address 0x70");
      while (true)
      {
         delay(100);
      }
   }

   Serial.println("I2C Multiplexor Scanner - Ready");
}

void loop()
{
   byte error, address;
   int nDevices;

   Serial.println("Scanning I2C multiplexor ports...");

   // Scan each port of the multiplexor
   for (uint8_t port = 0; port < NUM_MULTIPLEXOR_PORTS; port++)
   {
      // Select port on the multiplexor
      Wire.beginTransmission(MULTIPLEXOR_ADDR);
      Wire.write(1 << port);
      Wire.endTransmission();

      delay(10);  // Allow time for port switching

      Serial.print("Port ");
      Serial.print(port);

      nDevices = 0;

      // Scan this port for devices
      for (address = I2C_MIN_ADDR; address < I2C_MAX_ADDR; address++)
      {
         Wire.beginTransmission(address);
         error = Wire.endTransmission();

         if (error == 0)
         {
            Serial.print(" 0x");
            if (address < 16)
            {
               Serial.print("0");
            }
            Serial.print(address, HEX);

            nDevices++;
         }
         else if (error == 4)
         {
            Serial.print("\nUnknown error at address 0x");
            if (address < 16)
            {
               Serial.print("0");
            }
            Serial.println(address, HEX);
         }
      }

      if (nDevices == 0)
      {
         Serial.println(" No I2C devices found");
      }
      else
      {
         Serial.println();
      }
   }

   delay(SCAN_INTERVAL_MS);  // Wait before next scan
}
