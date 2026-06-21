#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>

Adafruit_7segment matrix = Adafruit_7segment();

void setup()
{
   matrix.begin(0x70); // Initialize the display at I2C address 0x70
}

void loop()
{
   // Display a number
   matrix.println(1234);
   matrix.writeDisplay();
   delay(1000);

   // Display a floating point number
   matrix.println(3.14);
   matrix.writeDisplay();
   delay(1000);
}