/// <summary>
/// Ultrasonic distance sensor display for Feather displays.
/// </summary>
/// <remarks>
/// Continuously measures distance using an HC-SR04 ultrasonic sensor and displays
/// the results in both centimeters and inches on a TFT display. Also logs timing
/// and measurement rate to the serial console.
/// 
/// Hardware: Feather ESP32 with TFT display, HC-SR04 ultrasonic sensor.
/// </remarks>

#include <Arduino.h>

#include "Feather.h"
#include "Rate.h"
#include "SerialX.h"

#include "WiFiSettings.h"

constexpr uint8_t TRIGGER_PIN = 6;
constexpr uint8_t ECHO_PIN = 5;

constexpr uint8_t NUM_DECIMALS = 1;
constexpr float CM_PER_MICROSECOND = 0.034f / 2.0f;  // Speed of sound conversion factor
constexpr float INCH_PER_CM = 1.0f / 2.54f;          // Centimeter to inch conversion

Feather feather;
Format distFormatCm("###.## cm");
Format distFormatIn("###.## in");
Rate rate;

void setup()
{
   feather.begin();
   SerialX::begin();
   Serial.println("Wave Display - Initializing");

   pinMode(TRIGGER_PIN, OUTPUT);
   pinMode(ECHO_PIN, INPUT);

   Serial.println("Wave Display - Ready");
}

void loop()
{
   rate.start();

   // Trigger ultrasonic measurement
   digitalWrite(TRIGGER_PIN, LOW);
   delayMicroseconds(2);

   digitalWrite(TRIGGER_PIN, HIGH);
   delayMicroseconds(10);
   digitalWrite(TRIGGER_PIN, LOW);

   // Measure echo duration and calculate distance
   long durationMicros = pulseIn(ECHO_PIN, HIGH);
   float distanceCM = durationMicros * CM_PER_MICROSECOND;
   float distanceIN = distanceCM * INCH_PER_CM;

   // Avoid echo interference on next measurement
   delayMicroseconds(2 * durationMicros);
   rate.stop();

   // Display distance values on TFT
   feather.setTextSize(5);
   feather.setCursor(0, 0);
   feather.println(distanceCM, distFormatCm, Color::VALUE);
   feather.println(distanceIN, distFormatIn, Color::VALUE);

   // Log measurement and performance metrics
   float elapsedMS = rate.elapsedMicros() / 1000.0f;
   float ratePerSec = rate.get();
   Serial.print("Distance: " + String(distanceCM) + " cm   " + String(distanceIN) + " in   ");
   Serial.println(String(elapsedMS) + " ms = " + String(ratePerSec) + " per sec");
}
