#pragma once

#include <AUnit.h>
#include "Tick.h"

/// <summary>
/// Verifies Tick elapsed-micros conversion aligns with Arduino micros() delta.
/// </summary>
test(TickTest, elapsedMicrosShouldMatchMicrosDelta)
{
   uint32_t startMicros = micros();
   uint32_t startTick = Tick::now();

   delay(5);

   uint32_t endMicros = micros();
   uint32_t endTick = Tick::now();

   uint32_t microsDelta = endMicros - startMicros;
   double tickMicros = Tick::elapsedMicros(startTick, endTick);

   assertNear((float) microsDelta, (float) tickMicros, 400.0f);
}

/// <summary>
/// Verifies Tick elapsed-millis conversion aligns with Arduino millis() delta.
/// </summary>
test(TickTest, elapsedMillisShouldMatchMillisDelta)
{
   uint32_t startMillis = millis();
   uint32_t startTick = Tick::now();

   delay(20);

   uint32_t endMillis = millis();
   uint32_t endTick = Tick::now();

   uint32_t millisDelta = endMillis - startMillis;
   double tickMillis = Tick::elapsedMillis(startTick, endTick);

   assertNear((float) millisDelta, (float) tickMillis, 2.0f);
}

/// <summary>
/// Verifies single-argument elapsedMicros(start) tracks micros() progression.
/// </summary>
test(TickTest, elapsedMicrosFromStartShouldTrackMicros)
{
   uint32_t startMicros = micros();
   uint32_t startTick = Tick::now();

   delay(3);

   uint32_t endMicros = micros();
   uint32_t microsDelta = endMicros - startMicros;
   double tickMicros = Tick::elapsedMicros(startTick);

   assertNear((float) microsDelta, (float) tickMicros, 400.0f);
}

/// <summary>
/// Verifies single-argument elapsedMillis(start) tracks millis() progression.
/// </summary>
test(TickTest, elapsedMillisFromStartShouldTrackMillis)
{
   uint32_t startMillis = millis();
   uint32_t startTick = Tick::now();

   delay(15);

   uint32_t endMillis = millis();
   uint32_t millisDelta = endMillis - startMillis;
   double tickMillis = Tick::elapsedMillis(startTick);

   assertNear((float) millisDelta, (float) tickMillis, 2.0f);
}

/// <summary>
/// Verifies span handles uint32 wrap-around correctly.
/// </summary>
test(TickTest, spanShouldHandleWrapAround)
{
   uint32_t start = 0xFFFFFFF0;
   uint32_t end = 0x00000010;

   uint32_t delta = Tick::span(start, end);

   assertEqual((uint32_t) 32, delta);
}

/// <summary>
/// Verifies micros and millis conversions are internally consistent.
/// </summary>
test(TickTest, elapsedMicrosAndMillisShouldBeConsistent)
{
   uint32_t startTick = Tick::now();

   delay(8);

   uint32_t endTick = Tick::now();
   double tickMicros = Tick::elapsedMicros(startTick, endTick);
   double tickMillis = Tick::elapsedMillis(startTick, endTick);

   assertNear((float) (tickMicros / 1000.0), (float) tickMillis, 0.25f);
}
