
#include <FeedTimer.h>
#include <NTPClient.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <Util.h>
#include <TimedAverager.h>
#include <I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_INA219.h>

// OLED buttons
#define BUTTON_A 9
#define BUTTON_B 6
#define BUTTON_C 5

// A7 is already rigged for Adafruit Feathers to split the battery
// volts with 100k resistors
//#define BATTERY_VOLTS_PIN PIN_A7
#define SOLAR_VOLTS_PIN PIN_A0
#define CHARGE_ENABLE_PIN 10 

// 4 line OLED display
Adafruit_SH1107 display(64, 128, &Wire);
Adafruit_INA219 ina;

TimedAverager solarVolts(30000);
TimedAverager batteryVolts(30000);
TimedAverager batteryMilliAmps(10000);

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

   Serial.println("Starting Solar Charger Sketch");
   display.println("Starting...");
   display.display();

   ina.begin();

   pinMode(BUTTON_C, INPUT_PULLUP);
   pinMode(CHARGE_ENABLE_PIN, OUTPUT);
   digitalWrite(CHARGE_ENABLE_PIN, LOW);

   analogReadResolution(12);
}

void loop() {

   display.clearDisplay();

   // read values
   float bVolts = ina.getBusVoltage_V();
   float bMA = ina.getCurrent_mA();

   float r1 = 22;
   float r2 = 99;
   float sVolts = Util::readVolts(SOLAR_VOLTS_PIN, 4096, (r1 + r2) / r1);

   // record values
   batteryVolts.set(bVolts);
   batteryMilliAmps.set(bMA);
   solarVolts.set(sVolts);

   digitalWrite(CHARGE_ENABLE_PIN, HIGH);

   display.setCursor(0, 0);
   display.setTextSize(1);

   // display values
   display.print(" Batt: ");
   display.print(batteryVolts.get(), 4);
   display.print(" ");
   display.print(bVolts, 4);
   //   display.println(" v");
   display.println();

   display.print("         ");
   display.print(batteryMilliAmps.get(), 1);
   display.print(" ");
   //display.println(" mA");
   display.print(bMA, 1);
   display.println();

   display.print("Solar: ");
   display.print(solarVolts.get(), 3);
   display.print(" ");
   display.print(sVolts, 3);
   display.println();

   digitalWrite(CHARGE_ENABLE_PIN, !digitalRead(BUTTON_C));
   if (!digitalRead(CHARGE_ENABLE_PIN)) {
      display.println();
      display.println("Enabled");
   }

   display.display();
}
