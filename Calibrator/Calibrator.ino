#include <Logger.h>
#include <Util.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_GFX.h>
//#include <Adafruit_SH110X.h>
#include <I2CMultiplexor.h>

//Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);

// logging mechanism
Logger Error(
   new SerialLogHandler()
   //   new DisplayLogHandler(logFeed)
);

// temperature sensors
Adafruit_SHT31 sht35_1;
Adafruit_SHT31 sht35_2;

I2CMultiplexor i2cMultiplexor;

void setup() {
   // start serial port
   Serial.begin(115200);

   // wait 5 seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
      ;
   delay(500);

   Serial.println("Starting Calibration Display Sketch");

   i2cMultiplexor.begin();
   i2cMultiplexor.scan();

   i2cMultiplexor.select(2);
   if (sht35_1.begin(0x44) == false) {
      Error.println("SHT35 (Temperature) sensor initialization failed");
   }
   else {
      Serial.println("SHT35 (Temperature) sensor initialization succeeded");
   }

   i2cMultiplexor.select(3);
   if (sht35_2.begin(0x44) == false) {
      Error.println("SHT35 (Temperature) sensor initialization failed");
   }
   else {
      Serial.println("SHT35 (Temperature) sensor initialization succeeded");
   }


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
}

void loop() {

   /*
   display.clearDisplay();
   display.setCursor(0, 0);
   display.printf("Bat: %4.3f\n", Util::readVolts(PIN_A7));
   display.printf("Out: %4.3f\n", Util::readVolts(PIN_A1));
   display.display();
*/

   Serial.println("Temperature");
   i2cMultiplexor.select(2);
   Serial.print(Util::C2F(sht35_1.readTemperature()));
   Serial.print("   ");
   i2cMultiplexor.select(3);
   Serial.print(Util::C2F(sht35_2.readTemperature()));
   Serial.print("   ");
   Serial.println();

   Serial.println("Humidity");
   i2cMultiplexor.select(2);
   Serial.print(sht35_1.readHumidity());
   Serial.print("%   ");
   i2cMultiplexor.select(3);
   Serial.print(sht35_2.readHumidity());
   Serial.print("%   ");
   Serial.println();

   delay(1000);
}
