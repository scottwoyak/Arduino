//
// Simple hardware wiring verification.
//
// Charges the capacitor directly and polls the sense pin to verify basic
// electrical connectivity, bypassing any background timer or interrupt libraries.
//

#include <Arduino.h>

constexpr uint8_t CHARGE_PIN = 6; // Configured for the 1M resistor
constexpr uint8_t SENSE_PIN = 5;

void setup()
{
   Serial.begin(115200);
   // Wait briefly for serial connection if needed
   delay(1000);

   Serial.println();
   Serial.println("=================================");
   Serial.println(" Capacitor Wiring Test Started");
   Serial.println("=================================");

   pinMode(CHARGE_PIN, OUTPUT);
   digitalWrite(CHARGE_PIN, LOW);
}

void loop()
{
   // 1. Actively discharge the capacitor
   pinMode(SENSE_PIN, OUTPUT);
   digitalWrite(SENSE_PIN, LOW);
   digitalWrite(CHARGE_PIN, LOW);

   // Wait 10ms to ensure complete discharge
   delay(10); 

   // 2. Prepare sense pin as high-impedance input
   pinMode(SENSE_PIN, INPUT);

   // 3. Start charging and capture start time
   unsigned long startMicros = micros();
   digitalWrite(CHARGE_PIN, HIGH);

   // 4. Poll until the sense pin transitions HIGH, with a 1-second timeout
   unsigned long currentMicros = startMicros;
   bool timeout = false;

   while (digitalRead(SENSE_PIN) == LOW)
   {
      currentMicros = micros();
      if (currentMicros - startMicros > 1000000UL) // 1,000,000 us = 1 second
      {
         timeout = true;
         break;
      }
   }

   // 5. Output results
   if (timeout)
   {
      Serial.println("TIMEOUT! Sense pin never triggered. Check wiring/resistor connections.");
   }
   else
   {
      unsigned long chargeTime = currentMicros - startMicros;
      Serial.print("SUCCESS! Charge time: ");
      Serial.print(chargeTime);
      Serial.println(" us");
   }

   // Delay before the next test cycle
   delay(1000);
}
