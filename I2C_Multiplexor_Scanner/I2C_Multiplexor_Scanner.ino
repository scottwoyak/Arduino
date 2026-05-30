
#include <Wire.h>

constexpr auto I2C_SCL = 8;
constexpr auto I2C_SDA = 7;

constexpr auto NUM_MULTIPLEXOR_PORTS = 8;

void setup()
{
#if defined ARDUINO_WAVESHARE_ESP32_S3_ZERO
#pragma message "Detected Waveshare ESP32-S3-Zero"
   Wire.begin(I2C_SDA, I2C_SCL);
#else
   Wire.begin();
#endif

   Serial.begin(115200);
   while (!Serial)
      delay(10);
}

void loop()
{
   byte error, address;
   int nDevices;

   Serial.println("Scanning...");

   for (uint8_t port = 0; port < NUM_MULTIPLEXOR_PORTS; port++)
   {
      Wire.beginTransmission(0x70); // default address of the multiplexor
      Wire.write(1 << port);        // select this port
      Wire.endTransmission();

      Serial.print("Port ");
      Serial.print(port);

      nDevices = 0;
      for (address = 1; address < 127; address++)
      {
         // The i2c_scanner uses the return value of
         // the Write.endTransmisstion to see if
         // a device did acknowledge to the address.
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

   delay(5000);           // wait 5 seconds for next scan
}