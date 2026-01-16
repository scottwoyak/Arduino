#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WindMeter.h>

Adafruit_SH1107 display(64, 128, &Wire);
WindMeter windMeter(A0);

// The setup() function runs once each time the micro-controller starts
void setup()
{
   pinMode(A0, INPUT_PULLDOWN);

   display.begin(0x3C, true); // Address 0x3C default
   display.setTextSize(1);
   display.setTextColor(SH110X_WHITE);
   display.setRotation(3);
   display.setCursor(0, 0);

   display.clearDisplay();
   display.write("Scott");
   display.display();

   windMeter.begin();
}

// Add the main program code into the continuous loop() function
void loop()
{
   display.clearDisplay();
   display.setCursor(0, 0);
   display.println("Wind Readings");
   display.print("Current: ");
   display.println(windMeter.getCurrent());
   display.print("Average: ");
   display.println(windMeter.getAvg());
   display.display();
   //digitalWrite(LED_BUILTIN, digitalRead(A0));
}
