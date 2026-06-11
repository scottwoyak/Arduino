#include <LGX_HosyondESP32-32E.h>

#include <Scott16.h>
#include <Scott32.h>
#include <RunningAverager.h>

LGFX display;
RunningAverager fps(100);

void setup()
{
   Serial.begin(115200);
   while (!Serial);

   display.init();

   Serial.println("--- LovyanGFX Autodetected Display Settings ---");
   Serial.printf("Display Width: %d px\n", display.width());
   Serial.printf("Display Height: %d px\n", display.height());
   Serial.printf("Color Depth: %d bits per pixel\n", display.getColorDepth());
   Serial.printf("Rotation State: %d\n", display.getRotation());
}

long lastMicros = micros();

// Add the main program code into the continuous loop() function
void loop()
{
   unsigned long newMicros = micros();
   fps.set(1000000.0 / (newMicros - lastMicros));

   display.setCursor(0, 0);

   //#define OLD_FONTS
#ifdef OLD_FONTS

   // this code is for drawing the traditional Adafruit style block fonts
   display.setTextSize(4);
   display.println(random(9999));
   display.println(random(9999));
   display.println(random(9999));

   display.setTextSize(2);
   display.setCursor(0, display.height() - 16);

#else

   // this code is for anti-aliased fonts

   display.loadFont(Scott32);
   display.setTextColor(TFT_WHITE, TFT_BLACK);
   display.println(random(9999));
   display.println(random(9999));
   display.println(random(9999));

   display.loadFont(Scott16);
   display.setCursor(0, display.height() - display.fontHeight());

#endif

   display.print("FPS: ");
   display.print(fps.get(), 1);
   display.print("  "); // erase any remaining characters from previous loop

   lastMicros = newMicros;
}
