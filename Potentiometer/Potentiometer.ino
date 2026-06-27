/// <summary>
/// Potentiometer input reading example with jitter filtering.
/// </summary>
/// <remarks>
/// Demonstrates reading analog potentiometer values with and without jitter filtering.
/// Outputs both raw float values and mapped integer values to serial console.
/// Shows the difference between filtered and unfiltered analog readings.
/// 
/// Hardware: Arduino with 10k potentiometer on analog pin A0.
/// </remarks>

#include <Arduino.h>

#include "Potentiometer.h"
#include "SerialX.h"

Potentiometer p(A0);
IntPotentiometer p2(A0, 0, 100);

void setup()
{
   SerialX::begin();

   Serial.println("Potentiometer Reader - Ready");
   Serial.println("Raw Value   Mapped Value (0-100)");
}

void loop()
{
   Serial.print(p.read());
   Serial.print("   ");
   Serial.print(p2.read());
   Serial.println();
}
