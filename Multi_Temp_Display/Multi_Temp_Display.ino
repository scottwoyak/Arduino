#include "Feather.h"
#include "TempSensor.h"
#include "Adafruit_SleepyDog.h"
#include "SerialX.h"
#include <string>
#include <I2CMultiplexor.h>
#include <Timer.h>

#include <WiFiSettings.h>

Format tempFormat("###.## F");

Feather feather;

I2CMultiplexor multi;

constexpr uint8_t NUM_SENSORS = 8;
TempSensor* sensors[NUM_SENSORS];

void setup()
{
   Wire.begin();
   SerialX::begin();

   // create all the objects
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensors[i] = new TempSensor();
   }

   feather.begin();
   feather.setRotation(DisplayRotation::PORTRAIT);
   feather.setTextSize(2);
   feather.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   feather.echoToSerial = true;
   feather.clearDisplay();
   feather.println("Initializing", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.print("Sensors... ", Color::LABEL);
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      Serial.println();
      Serial.print("Sensor ");
      Serial.println(i);
      multi.select(i);
      if (sensors[i]->begin(true))
      {
         Serial.print("         Type: ");
         Serial.println(sensors[i]->type());
         Serial.print("      Address: ");
         Serial.println(sensors[i]->address());
         Serial.print("           ID: ");
         Serial.println(sensors[i]->id());
         Serial.print("   Correction: ");
         Serial.println(sensors[i]->tempCorrectionF(), 3);
      }
      else
      {
         // TODO null out this sensor
         Serial.println("FAILED");
      }
   }
   feather.printlnR("ok", Color::VALUE);

   feather.clearDisplay();
   feather.echoToSerial = false;
}

void loop()
{
   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.print("MultiTemp", Color::HEADING);
   feather.println();

   feather.setTextSize(3);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (sensors[i]->exists())
      {
         multi.select(i);
         float temp = sensors[i]->readTemperatureF();
         feather.println(temp, tempFormat, Color::VALUE);
         Serial.print(i);
         Serial.print(": ");
         Serial.println(temp, 2);
      }
      else
      {
         feather.println("----", Color::GRAY);
      }
   }
}


