#include "Feather.h"
#include "Rate.h"

Feather feather;
constexpr uint8_t TRIGGER_PIN = 6;
constexpr uint8_t ECHO_PIN = 5;

Format distFormat("###.## cm");
Format rateFormat("####/s");

Rate rate;

// The setup() function runs once each time the micro-controller starts
void setup()
{
   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

   feather.begin();

   pinMode(TRIGGER_PIN, OUTPUT);
   pinMode(ECHO_PIN, INPUT);
}

void loop()
{
   rate.start();
   digitalWrite(TRIGGER_PIN, LOW);
   delayMicroseconds(2);

   digitalWrite(TRIGGER_PIN, HIGH); 
   delayMicroseconds(10);
   digitalWrite(TRIGGER_PIN, LOW);

   long durationMicros = pulseIn(ECHO_PIN, HIGH);
   rate.stop();
   float timeMs = rate.elapsedMicros() / 1000.0f;
   float distance = durationMicros * 0.034 / 2;



   delay(100);
   Serial.println("Duration: " + String(durationMicros) + " micros");
   Serial.println("Distance: " + String(distance) + " cm");
   Serial.println("Time: " + String(timeMs) + " ms");

   feather.setCursor(0, 0);

   // display values
   feather.setTextSize(5);
   feather.println(distance, distFormat, Color::VALUE);


   feather.setTextSize(3);
   feather.setCursorY(-feather.charH());
   feather.println(rate.get(), rateFormat, Color::SUB_LABEL);
   /*
   feather.setTextSize(2);
   feather.print(tempTime, msFormat, Color::SUB_LABEL);
   feather.print("  ");
   feather.print(tempPerS, rateFormat, Color::SUB_LABEL);
   feather.println();
   feather.moveCursorY(feather.charH() / 2);

   feather.setTextSize(3);
   if (feather.display.width() / feather.charW() > 12)
   {
      feather.print(" Hum: ", Color::LABEL);
   }
   feather.println(hum, humFormat, Color::VALUE);

   feather.setTextSize(2);
   feather.print(humTime, msFormat, Color::SUB_LABEL);
   feather.print("  ");
   feather.print(humPerS, rateFormat, Color::SUB_LABEL);
   feather.println();

   feather.setTextSize(2);
   feather.setCursor(0, -2*feather.charH() + 1);
   feather.print("Type: ", sensor.type(), Color::VALUE2);
   feather.print(" 0x", Color::VALUE2);
   feather.println(sensor.address(), HEX, Color::VALUE2);

   feather.moveCursorY(1);
   feather.println("  ID: ", sensor.id(), Color::VALUE2);
   */
}


