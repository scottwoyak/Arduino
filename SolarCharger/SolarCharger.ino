
#include <FeedTimer.h>
#include <NTPClient.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <Util.h>
#include <RunningAverager.h>
#include <I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_INA219.h>

// OLED buttons
#define BUTTON_A 9
#define BUTTON_B 6
#define BUTTON_C 5

// wind meter class
// display class
// integrated wing class

// A7 is already rigged for Adafruit Feathers to split the battery
// volts with 100k resistors
//#define BATTERY_VOLTS_PIN PIN_A7
#define SOLAR_VOLTS_PIN PIN_A0
#define CHARGE_ENABLE_PIN 10 

// 4 line OLED display
Adafruit_SH1107 display(64, 128, &Wire);
Adafruit_INA219 ina;

#define NUM_SAMPLES 1
RunningAverager solarVolts(NUM_SAMPLES);
RunningAverager batteryVolts(NUM_SAMPLES);
RunningAverager batteryMilliAmps(NUM_SAMPLES);

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

#define DELAY 1000
void loop() {


   display.clearDisplay();

   float r1 = 22;
   float r2 = 99.6;
   solarVolts.set(Util::readVolts(SOLAR_VOLTS_PIN, 4096, (r1 + r2) / r1));
   batteryVolts.set(ina.getBusVoltage_V());
   batteryMilliAmps.set(ina.getCurrent_mA());


   digitalWrite(CHARGE_ENABLE_PIN, HIGH);
   delay(DELAY);
   float sv = Util::readVolts(SOLAR_VOLTS_PIN, 4096, (r1 + r2) / r1);
   float bv = ina.getBusVoltage_V();
   float bm = ina.getCurrent_mA();

   display.setCursor(0, 0);
   display.setTextSize(1);

   display.print(" Batt: ");
   display.print(batteryVolts.get(), 4);
   display.print(" ");
   display.print(bv, 4);
   //   display.println(" v");
   display.println();

   display.print("         ");
   display.print(batteryMilliAmps.get(), 1);
   display.print(" ");
   //display.println(" mA");
   display.print(bm, 1);
   display.println();

   display.print("Solar: ");
   display.print(solarVolts.get(), 3);
   display.print(" ");
   //display.println(" v");
   display.print(sv, 3);
   display.println();

   digitalWrite(CHARGE_ENABLE_PIN, !digitalRead(BUTTON_C));
   if (!digitalRead(CHARGE_ENABLE_PIN)) {
      display.println();
      display.println("Enabled");
   }

   display.display();
   delay(DELAY);
}
