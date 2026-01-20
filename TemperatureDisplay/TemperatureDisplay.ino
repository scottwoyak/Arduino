#include "Feather_ESP32_S3.h"
#include "TempSensor.h"
#include "RunningAverager.h"

Feather_ESP32_S3 feather;

ITempSensor* sensor;

// The setup() function runs once each time the micro-controller starts
void setup()
{
   Wire.begin();

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

   feather.begin();

   sensor = TempSensorFactory::create();
   sensor->begin();
}

void loop()
{
   feather.setCursor(0, 0);

   feather.display.setTextSize(4);
   feather.println(sensor->readTemperatureF(), " F", 8, Color565::WHITE);
   feather.println(sensor->readHumidity(), "%", 7, Color565::WHITE);

   feather.display.setTextSize(2);
   feather.setCursor(0, feather.display.height() - 16);
   feather.print(sensor->info(), Color565::GRAY);
}


