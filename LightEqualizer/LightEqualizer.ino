
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Stopwatch.h>

#define BUTTON_A 9
#define BUTTON_B 6
#define BUTTON_C 5

Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);

Stopwatch sw;


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

   pinMode(BUTTON_A, INPUT_PULLUP);
   pinMode(BUTTON_B, INPUT_PULLUP);
   pinMode(BUTTON_C, INPUT_PULLUP);
   pinMode(10, OUTPUT);

   pinMode(PIN_A0, INPUT);
   pinMode(PIN_A1, INPUT);
   pinMode(PIN_A2, INPUT);
}

void loop() {


   float min = 500;
   float max = 1000;

   float l1 = (analogRead(PIN_A0) - min) / (max - min);
   l1 = constrain(l1, 0, 1);
   float l2 = (analogRead(PIN_A1) - min) / (max - min);
   l2 = constrain(l2, 0, 1);
   float l3 = (analogRead(PIN_A2) - min) / (max - min);
   l3 = constrain(l3, 0, 1);

   float maxMs = 1000;
   float minMs = 5;
   float ms = minMs + (maxMs - minMs) * l1;

   if (sw.elapsedMillis() > ms) {
      sw.reset();
      digitalWrite(10, digitalRead(10) == LOW);
   }

   //if (digitalRead(BUTTON_A) == LOW) {
   display.clearDisplay();
   display.setCursor(0, 0);
   display.setTextSize(2);
   display.println(l1);
   display.println(l2);
   display.println(l3);
   display.display();
   //}
}
