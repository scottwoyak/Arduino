#include "Feather.h"
#include "Stopwatch.h"

#include "TempSensor.h"

// 
// This sketch displays the current temperature on an Arduino ESP32 Feather
//
Feather feather;
TempSensor sensor;

// create this define to use a DS18B20 sensor. If not defined, one of the I2C sensors will be auto detected
//#define ONE_WIRE_PIN 5

Format tempFormat("###.## F");
Format humFormat("###.#%");
Format rateFormat("####/s");
Format msFormat("####.# ms");

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

#ifdef ONE_WIRE_PIN
   sensor.begin(5, true);
#else
   sensor.begin();
#endif
}

Stopwatch sw;

void loop()
{
   feather.setCursor(0, 0);

   // read sensor
   sw.reset();
   float temp = sensor.readTemperatureF();
   float tempTime = sw.elapsedMillis();
   int16_t tempPerS = round(1000 / tempTime);

   sw.reset();
   float hum = sensor.readHumidity();
   float humTime = sw.elapsedMillis();
   int16_t humPerS = round(1000 / humTime);

   // display values
   feather.setTextSize(3);
   if (feather.display.width() / feather.charW() > 12)
   {
      feather.print("Temp: ", Color::LABEL);
   }
   feather.println(temp, tempFormat, Color::VALUE);

   feather.setTextSize(2);
   feather.print(tempTime, msFormat, Color::SUB_LABEL);
   feather.print("  ");
   feather.print(tempPerS, rateFormat, Color::SUB_LABEL);
   feather.println();
   feather.moveCursorY(feather.charH() / 2);

   feather.setTextSize(3);
   if (feather.display.width() / feather.charW() > 12)
   {
      feather.print(" Hum: ", Color::LABEL);
   }
   feather.println(hum, humFormat, Color::VALUE);

   feather.setTextSize(2);
   feather.print(humTime, msFormat, Color::SUB_LABEL);
   feather.print("  ");
   feather.print(humPerS, rateFormat, Color::SUB_LABEL);
   feather.println();

   feather.setTextSize(2);
   feather.setCursor(0, -2*feather.charH() + 1);
   feather.print("Type: ", Color::LABEL);
   feather.print(sensor.type(), Color::VALUE2);
   feather.print(" 0x", Color::VALUE2);
   feather.println(sensor.address(), HEX, Color::VALUE2);

   feather.moveCursorY(1);
   feather.print("  ID: ", Color::LABEL);
   feather.print(sensor.id(), Color::VALUE2);
}


