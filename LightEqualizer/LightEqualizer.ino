
#include <I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <hp_BH1750.h>
#include <I2CMultiplexor.h>


Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);
hp_BH1750 BH1750[8];
I2CMultiplexor multi;

void setup() {
   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

   display.begin(0x3C, true); // Address 0x3C default
   display.setTextSize(1);
   display.setTextColor(SH110X_WHITE);
   display.clearDisplay();
   display.setRotation(1);
   display.setCursor(0, 0);
   display.display(); // actually display all of the above   

   Serial.println("Starting Light Sketch");

   multi.begin();

   for (int i = 0; i < 8;i++) {
      multi.select(i);
      bool avail = BH1750[i].begin(BH1750_TO_GROUND);
      Serial.print(i);
      Serial.print(": ");
      if (avail == false) {
         Serial.println("No Sensor");
      }
      else {
         Serial.println("Sensor Found");
      }
   }
}

void loop() {

   display.clearDisplay();
   display.setCursor(0, 0);
   display.setTextSize(2);

   for (int i = 0; i < 3; i++) {
      multi.select(i);
      BH1750[i].start();
      float lux = BH1750[i].getLux();
      display.println(lux);
   }

   display.display();
}
