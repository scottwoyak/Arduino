
#include "TempSensor.h"
#include "Util.h"

ITempSensor* sensor;

void setup() {
   Wire.begin();

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(5000);

   sensor = TempSensorFactory::create();
   Serial.print("sensor.begin()");
   if (sensor->begin() == false) {
      Serial.println("Temperature/Humidity sensor initialization failed");
   }
   else {
      Serial.println(" - ok");
   }
}

void loop() {

   float temp = sensor->readTemperatureF();

   Serial.println(temp);
}
