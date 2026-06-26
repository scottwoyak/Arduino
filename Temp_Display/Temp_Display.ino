#include "Feather.h"
#include "Rate.h"

#include "TempSensor.h"

// 
// This sketch displays the current temperature on an Arduino ESP32 Feather
//
Feather feather;
TempSensor sensor;

// use this define to use a DS18B20 sensor. If not defined, one of the I2C sensors will be auto detected
//#define ONE_WIRE_PIN 5

Format tempFormat("###.## F");
Format humFormat("###.#%");
Format rateFormat("####/s");
Format msFormat("####.# ms");

Rate tempRate;
Rate humRate;

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
   sensor.begin(ONE_WIRE_PIN, true);
#else
   sensor.begin();
#endif

   if (sensor.exists() == false)
   {
      Serial.println("No sensor detected");
   }
   else
   {
      Serial.println("Sensor:");
      Serial.println("  Type: " + String(sensor.type()));
      Serial.println("  ID: " + String(sensor.id()));
      Serial.println("  Address: 0x" + String(sensor.address(), HEX));
   }
}

void loop()
{
   feather.setCursor(0, 0);

   // read sensor
   tempRate.start();
   float temp = sensor.readTemperatureF();
   tempRate.stop();
   float tempTime = tempRate.elapsedMicros() / 1000.0f;

   humRate.start();
   float hum = sensor.readHumidity();
   humRate.stop();
   float humTime = humRate.elapsedMicros() / 1000.0f;

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
   feather.print(tempRate.get(), rateFormat, Color::SUB_LABEL);
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
   feather.print(humRate.get(), rateFormat, Color::SUB_LABEL);
   feather.println();

   feather.setTextSize(2);
   feather.setCursor(0, -2*feather.charH() + 1);
   feather.print("Type: ", sensor.type(), Color::VALUE2);
   feather.print(" 0x", Color::VALUE2);
   feather.println(sensor.address(), HEX, Color::VALUE2);

   feather.moveCursorY(1);
   feather.println("  ID: ", sensor.id(), Color::VALUE2);
}


