#include "ESP32_S3_Playground.h"
#include "RollingRate.h"
#include "SerialX.h"

ESP32_S3_Playground arduino;
RollingRate fps(100);

Format posFormat("+######");

void setup()
{
   SerialX::begin();
   arduino.begin();
}

void loop()
{
   fps.tick();

   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println("Value: ", arduino.encoder.getPosition(), posFormat);

   arduino.setTextSize(2);
   arduino.print("FPS: ");
   arduino.print(fps.get(), 1);
   arduino.print("  "); // erase any remaining characters from previous loop
}
