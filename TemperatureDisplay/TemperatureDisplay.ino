#include "Feather_ESP32_S3.h"
#include "Stopwatch.h"
#include "TempSensor.h"
#include "RunningAverager.h"

// 
// This sketch displays the current temperature on an Arduino ESP32 Feather
//
Feather_ESP32_S3 feather;
TempSensor sensor;

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
   sensor.begin();
}

Stopwatch sw;

void loop()
{
   sw.reset();
   float temp = sensor.readTemperatureF();
   float tempTime = sw.elapsedMillis();

   sw.reset();
   float hum = sensor.readHumidity();
   float humTime = sw.elapsedMillis();

   feather.setCursor(0, 0);
   feather.display.setTextSize(4);
   feather.println(temp, " F", 8, Color::WHITE);
   feather.display.setTextSize(2);
   feather.println(tempTime, " ms", 8, Color::GRAY);

   feather.moveCursorY(10);
   feather.display.setTextSize(4);
   feather.println(hum, "%", 7, Color::WHITE);
   feather.display.setTextSize(2);
   feather.println(humTime, " ms", 8, Color::GRAY);


   feather.display.setTextSize(2);
   feather.setCursor(0, feather.display.height() - 16);
   feather.print(sensor.info(), Color::CYAN);
}


