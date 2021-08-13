#include <Logger.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// logging mechanism
Logger Error(
   new SerialLogHandler()
   //   new DisplayLogHandler(logFeed)
);

class OLEDStarterSketch {
public:
   void setup() {
      // start serial port
      Serial.begin(115200);

      // wait 5 seconds for the serial monitor to open
      while (millis() < 2000 && !Serial)
         ;
      delay(500);

      Serial.println("Starting My Sketch");

      // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
      if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
         Error.println("SSD1306 allocation failed");
         for (;;); // Don't proceed, loop forever
      }

      // Clear the buffer.
      display.clearDisplay();
      display.display();

      // text display tests
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
   }

   int count = 0;
   void loop() {

      display.clearDisplay();
      display.setCursor(0, 0);
      display.print(count++);
      display.display();
   }
};