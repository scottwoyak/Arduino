#include <Adafruit_SH110X.h>
#include "TempSensor.h"

Adafruit_SH1107 display(64, 128, &Wire);
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

   display.begin(0x3C, true); // Address 0x3C default
   display.setTextColor(SH110X_WHITE);
   display.setRotation(1);
   display.clearDisplay();
   display.display();

   sensor = TempSensorFactory::create();
   Serial.print("sensor.begin()");
   if (sensor->begin() == false) {
      Serial.println("Temperature/Humidity sensor initialization failed");
   }
   else {
      Serial.println(" - ok");
   }
}

// Add the main program code into the continuous loop() function
void loop()
{
   display.clearDisplay();
   display.setTextSize(2);
   display.setCursor(0, 0);
   display.print(sensor->readTemperatureF());
   display.display();
}
