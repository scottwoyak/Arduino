/// <summary>
/// HC-SR04 ultrasonic distance sensor display.
/// </summary>
/// <remarks>
/// Measures distance using an HC-SR04 ultrasonic sensor and displays the result
/// in centimeters on a TFT display. Also logs distance, duration, and measurement
/// rate to the serial console. Includes measurement performance timing.
/// 
/// Hardware: Feather ESP32 with TFT display and HC-SR04 ultrasonic sensor.
/// </remarks>

#include <Arduino.h>

#include "Feather.h"
#include "Rate.h"
#include "SerialX.h"

constexpr uint8_t TRIGGER_PIN = 6;
constexpr uint8_t ECHO_PIN = 5;

constexpr float CM_PER_MICROSECOND = 0.034f / 2.0f;  // Speed of sound conversion

Feather feather;
Rate rate;

Format distFormat("###.## cm");
Format rateFormat("####/s");

void setup()
{
   SerialX::begin();

   feather.begin();

   pinMode(TRIGGER_PIN, OUTPUT);
   pinMode(ECHO_PIN, INPUT);

   Serial.println("SR04 Distance Sensor - Ready");
}

void loop()
{
   rate.start();

   // Trigger measurement
   digitalWrite(TRIGGER_PIN, LOW);
   delayMicroseconds(2);

   digitalWrite(TRIGGER_PIN, HIGH);
   delayMicroseconds(10);
   digitalWrite(TRIGGER_PIN, LOW);

   // Measure echo duration and calculate distance
   long durationMicros = pulseIn(ECHO_PIN, HIGH);
   rate.stop();

   float timeMs = rate.elapsedMicros() / 1000.0f;
   float distance = durationMicros * CM_PER_MICROSECOND;

   delay(100);

   // Log metrics
   Serial.println("Duration: " + String(durationMicros) + " micros");
   Serial.println("Distance: " + String(distance) + " cm");
   Serial.println("Time: " + String(timeMs) + " ms");

   // Display distance value
   feather.setCursor(0, 0);
   feather.setTextSize(5);
   feather.println(distance, distFormat, Color::VALUE);

   // Display measurement rate
   feather.setTextSize(3);
   feather.setCursorY(-feather.charH());
   feather.println(rate.get(), rateFormat, Color::SUB_LABEL);
}
