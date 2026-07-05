//-------------------------------------------------------------------------------------------------
//
// Displays temperatures from up to 8 I2C-multiplexed sensors.
// Hold button A to show sensor type instead of temperature.
//
//-------------------------------------------------------------------------------------------------
#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "TempSensor.h"
#include "SerialX.h"
#include "I2CMultiplexor.h"

Format tempFormat("###.## F");

Arduino arduino;
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

   arduino.begin();
   arduino.setRotation(DisplayRotation::PORTRAIT);
   arduino.setTextSize(INDEX_TEXT_SIZE);
   arduino.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   arduino.echoToSerial = true;
   arduino.clearDisplay();
   arduino.println("Initializing", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 2);

   arduino.print("Sensors... ", Color::LABEL);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      printSensorInitInfo(i);
   }
   arduino.printlnR("ok", Color::VALUE);

   arduino.clearDisplay();
   arduino.echoToSerial = false;
}

void loop()
{
   const bool showType = arduino.buttonA.isPressed();
   static bool lastShowType = showType;

   if (showType != lastShowType)
   {
      arduino.clearDisplay();
      lastShowType = showType;
   }

   arduino.setCursor(0, 0);
   arduino.setTextSize(TITLE_TEXT_SIZE);
   arduino.print("MultiTemp", Color::HEADING);
   arduino.println();
   arduino.moveCursorY(arduino.charH() / 3);

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      arduino.setTextSize(INDEX_TEXT_SIZE);
      arduino.print(i, Color::GRAY);
      arduino.print(" ");
      arduino.setTextSize(VALUE_TEXT_SIZE);

      if (!sensors[i]->exists())
      {
         arduino.println("----", Color::GRAY);
         continue;
      }

      multi.select(i);
      if (showType)
      {
         arduino.println(sensors[i]->type(), Color::VALUE);
      }
      else
      {
         float temp = sensors[i]->readTemperatureF();
         arduino.println(temp, tempFormat, Color::VALUE);
         Serial.print(i);
         Serial.print(": ");
         Serial.println(temp, 2);
      }
   }
}


