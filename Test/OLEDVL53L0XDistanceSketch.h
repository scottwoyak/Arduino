#include <Logger.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Util.h>
#include <Adafruit_VL53L0X.h>
#include <RunningAverager.h>

Adafruit_VL53L0X lox;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);


// logging mechanism
Logger Error(
   new SerialLogHandler()
   //   new DisplayLogHandler(logFeed)
);

RunningAverager distance(10);

class OLEDVL53L0XDistanceSketch {
public:
   void setup() {
      // start serial port
      Serial.begin(115200);

      // wait 5 seconds for the serial monitor to open
      while (millis() < 2000 && !Serial)
         ;
      delay(500);

      Serial.println("Starting Sketch");

      // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
      if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
         Error.println("SSD1306 allocation failed");
         for (;;); // Don't proceed, loop forever
      }

      lox.begin();

      // Clear the buffer.
      display.clearDisplay();
      display.display();

      // text display tests
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
   }

   void loop() {

      display.clearDisplay();
      display.setCursor(0, 0);

      VL53L0X_RangingMeasurementData_t measure;
      lox.rangingTest(&measure, false); // pass in 'true' to get debug data printout!

      if (measure.RangeStatus != 4) {  // phase failures have incorrect data
         distance.set(measure.RangeMilliMeter);
      }
      else {
         // out of range
         distance.set(NAN);
      }

      display.print(distance.get(), 0);
      display.print(" MM");

      display.display();
   }
};