#include <Logger.h>
#include <Util.h>
#include <SPI.h>
#include <Wire.h>
//#include <Adafruit_GFX.h>
//#include <Adafruit_SH110X.h>

#define TCAADDR 0x70

void tcaselect(uint8_t i) {
   if (i > 7) return;

   //Serial.println("beingTransmission");
   Wire.beginTransmission(TCAADDR);
   //Serial.println("write");
   Wire.write(1 << i);
   //Serial.println("done");
   Wire.endTransmission();
   //Serial.println("end");
}

//Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);

// logging mechanism
Logger Error(
   new SerialLogHandler()
   //   new DisplayLogHandler(logFeed)
);

void setup() {
   // start serial port
   Serial.begin(115200);

   // wait 5 seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
      ;
   delay(500);

   Serial.println("Starting Calibration Display Sketch");


   /*
   display.begin(0x3C, true); // Address 0x3C default

      // Clear the buffer.
   display.clearDisplay();
   display.display();

   display.setRotation(1);

   // text display tests
   display.setTextSize(2);
   display.setTextColor(SH110X_WHITE);
*/

   Wire.begin();
   delay(500);

   Serial.begin(115200);
   Serial.println("\nTCAScanner ready!");

   for (uint8_t t = 0; t < 8; t++) {
      tcaselect(t);
      Serial.print("TCA Port #"); Serial.println(t);
      delay(100);

      for (uint8_t addr = 0; addr <= 127; addr++) {
         //if (addr == TCAADDR) continue; // us - the multiplexor
         //if (addr == 0x3C) continue; // OLED display

         Wire.beginTransmission(addr);
         if (!Wire.endTransmission()) {
            Serial.print("    0x");  Serial.println(addr, HEX);
         }
      }
   }
   Serial.println("\ndone");
}

void loop() {

   /*
   display.clearDisplay();
   display.setCursor(0, 0);
   display.printf("Bat: %4.3f\n", Util::readVolts(PIN_A7));
   display.printf("Out: %4.3f\n", Util::readVolts(PIN_A1));
   display.display();
*/

   delay(100);
}
