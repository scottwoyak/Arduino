//-------------------------------------------------------------------------------------------------
//
// Displays temperatures from up to 8 I2C-multiplexed sensors.
// Hold button A to show sensor type instead of temperature.
//
//-------------------------------------------------------------------------------------------------
#include "Feather.h"
#include "TempSensor.h"
#include "SerialX.h"
#include "I2CMultiplexor.h"

Format tempFormat("###.## F");

Feather feather;
I2CMultiplexor multi;

constexpr uint8_t NUM_SENSORS = 8;
constexpr uint8_t TITLE_TEXT_SIZE = 3;
constexpr uint8_t INDEX_TEXT_SIZE = 2;
constexpr uint8_t VALUE_TEXT_SIZE = 3;

TempSensor* sensors[NUM_SENSORS];

void printSensorInitInfo(uint8_t index)
{
   Serial.println();
   Serial.print("Sensor ");
   Serial.println(index);

   multi.select(index);
   if (sensors[index]->begin(true))
   {
      Serial.print("         Type: ");
      Serial.println(sensors[index]->type());
      Serial.print("      Address: ");
      Serial.println(sensors[index]->address());
      Serial.print("           ID: ");
      Serial.println(sensors[index]->id());
      Serial.print("   Correction: ");
      Serial.println(sensors[index]->tempCorrectionF(), 3);
      return;
   }

   Serial.println("FAILED");
}

void setup()
{
   Wire.begin();
   SerialX::begin();

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensors[i] = new TempSensor();
   }

   feather.begin();
   feather.setRotation(DisplayRotation::PORTRAIT);
   feather.setTextSize(INDEX_TEXT_SIZE);
   feather.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   feather.echoToSerial = true;
   feather.clearDisplay();
   feather.println("Initializing", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.print("Sensors... ", Color::LABEL);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      printSensorInitInfo(i);
   }
   feather.printlnR("ok", Color::VALUE);

   feather.clearDisplay();
   feather.echoToSerial = false;
}

void loop()
{
   const bool showType = feather.buttonA.isPressed();
   static bool lastShowType = showType;

   if (showType != lastShowType)
   {
      feather.clearDisplay();
      lastShowType = showType;
   }

   feather.setCursor(0, 0);
   feather.setTextSize(TITLE_TEXT_SIZE);
   feather.print("MultiTemp", Color::HEADING);
   feather.println();
   feather.moveCursorY(feather.charH() / 3);

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      feather.setTextSize(INDEX_TEXT_SIZE);
      feather.print(i, Color::GRAY);
      feather.print(" ");
      feather.setTextSize(VALUE_TEXT_SIZE);

      if (!sensors[i]->exists())
      {
         feather.println("----", Color::GRAY);
         continue;
      }

      multi.select(i);
      if (showType)
      {
         feather.println(sensors[i]->type(), Color::VALUE);
      }
      else
      {
         float temp = sensors[i]->readTemperatureF();
         feather.println(temp, tempFormat, Color::VALUE);
         Serial.print(i);
         Serial.print(": ");
         Serial.println(temp, 2);
      }
   }
}


