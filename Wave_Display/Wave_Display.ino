#include "Feather.h"

#include "SerialX.h"
#include "WiFiSettings.h"
#include "Stopwatch.h"

Feather feather;
constexpr uint8_t TRIGGER_PIN = 6;
constexpr uint8_t ECHO_PIN = 5;

Format distFormatCm("###.## cm");
Format distFormatIn("###.## in");

constexpr auto NUM_DECIMALS = 1;

void setup()
{
   feather.begin();
   SerialX::begin();
   Serial.println("Wave Display");

   pinMode(TRIGGER_PIN, OUTPUT);
   pinMode(ECHO_PIN, INPUT);
}


Stopwatch sw;

void loop()
{
   digitalWrite(TRIGGER_PIN, LOW);
   delayMicroseconds(2);

   digitalWrite(TRIGGER_PIN, HIGH);
   delayMicroseconds(10);
   digitalWrite(TRIGGER_PIN, LOW);

   long durationMicros = pulseIn(ECHO_PIN, HIGH);
   float distanceCM = durationMicros * 0.034 / 2;
   float distanceIN = distanceCM * (1 / 2.54);

   // avoid echos
   delayMicroseconds(2*durationMicros);

   feather.setTextSize(5);
   feather.setCursor(0, 0);
   feather.println(distanceCM, distFormatCm, Color::VALUE);
   feather.println(distanceIN, distFormatIn, Color::VALUE);

   Serial.print("Distance: " + String(distanceCM) + " cm   " + String(distanceIN) + " in   ");
   Serial.println(sw.elapsedMillis() + String(" ms = ") + String(1000.0 / sw.elapsedMillis()) + " per sec");
   sw.reset();
}
