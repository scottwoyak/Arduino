//
// Simple hardware wiring verification.
//
// Charges the capacitor directly through each candidate charge resistor in turn and polls the
// sense pin to verify basic electrical connectivity, bypassing any background timer or interrupt
// libraries.
//

#include <Arduino.h>

constexpr uint8_t SENSE_PIN = 7;

///
/// <summary>
/// A charge pin paired with a human-readable label for the resistor it drives.
/// </summary>
///
struct ChargeResistor
{
   uint8_t pin;
   const char* label;
};

constexpr ChargeResistor CHARGE_RESISTORS[] = {
   { 15, "1M" },
   { 16, "470K" },
   { 2, "100K" },
   { 1, "47K" },
};
constexpr size_t CHARGE_RESISTOR_COUNT = sizeof(CHARGE_RESISTORS) / sizeof(CHARGE_RESISTORS[0]);

void setup()
{
   Serial.begin(115200);
   // Wait briefly for serial connection if needed
   delay(1000);

   Serial.println();
   Serial.println("=================================");
   Serial.println(" Capacitor Wiring Test Started");
   Serial.println("=================================");

   // Leave all charge pins high-impedance until each is tested in turn.
   for (size_t i = 0; i < CHARGE_RESISTOR_COUNT; i++)
   {
      pinMode(CHARGE_RESISTORS[i].pin, INPUT);
   }
}

void loop()
{
   for (size_t i = 0; i < CHARGE_RESISTOR_COUNT; i++)
   {
      uint8_t chargePin = CHARGE_RESISTORS[i].pin;

      Serial.print("--- Testing ");
      Serial.print(CHARGE_RESISTORS[i].label);
      Serial.println(" resistor ---");

      pinMode(chargePin, OUTPUT);
      digitalWrite(chargePin, LOW);

      // 1. Actively discharge the capacitor
      pinMode(SENSE_PIN, OUTPUT);
      digitalWrite(SENSE_PIN, LOW);
      digitalWrite(chargePin, LOW);

      // Wait 10ms to ensure complete discharge
      delay(10);

      // 2. Prepare sense pin as high-impedance input
      pinMode(SENSE_PIN, INPUT);

      // 3. Start charging and capture start time
      unsigned long startMicros = micros();
      digitalWrite(chargePin, HIGH);

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

      // Return this charge pin to high-impedance before testing the next resistor.
      digitalWrite(chargePin, LOW);
      pinMode(chargePin, INPUT);

      // Delay before the next resistor test
      delay(1000);
   }

   Serial.println();
}

