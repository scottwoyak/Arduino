#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

Adafruit_SH1107 display(64, 128, &Wire);

// The setup() function runs once each time the micro-controller starts
void setup()
{
   display.begin(0x3C, true); // Address 0x3C default
   display.setTextColor(SH110X_WHITE);
   display.setRotation(1);
   display.clearDisplay();
   display.display();
}

long counter = 0;
long lastMicros = micros();

// Add the main program code into the continuous loop() function
void loop()
{
   long newMicros = micros();
   double fps = 1000000.0 / (newMicros - lastMicros);

   display.clearDisplay();
   display.setTextSize(2);
   display.setCursor(0, 0);
   display.print(counter++);
   display.setTextSize(1);
   display.setCursor(0, display.height() - 8);
   display.print(fps);
   display.print(" fps");
   display.display();

   lastMicros = newMicros;
}
