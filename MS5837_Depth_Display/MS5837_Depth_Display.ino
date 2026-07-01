//
// Displays MS5837 water depth delta in centimeters relative to startup level.
//
// Detailed behavior:
// - Initializes the I2C bus, Feather display, and MS5837 pressure sensor.
// - Retries sensor initialization until successful, showing a visible error message on display.
// - Uses fresh-water density for depth conversion and stores startup depth as the zero reference.
// - Continuously reads depth and displays delta depth (currentDepthCm - baselineDepthCm).
//
// Display output:
// - Title row: "Water Delta".
// - Baseline row: fixed "Baseline: 0 cm" reference.
// - Main value row: signed depth delta in centimeters.
//
// Typical usage:
// - Place sensor at reference water level, then power/reset the board.
// - The startup reading becomes 0 cm.
// - Observe positive/negative delta values as the water level moves from that reference.
//
#include <Arduino.h>
#include <Wire.h>
#include <MS5837.h>
#include "Feather.h"

Feather feather;
MS5837 sensor;

Format deltaDepthFormat("####.## cm");

constexpr float FRESH_WATER_DENSITY_KG_PER_M3 = 997.0f;
constexpr float METERS_TO_CENTIMETERS = 100.0f;
constexpr uint16_t DISPLAY_REFRESH_DELAY_MS = 100;

float baselineDepthCm = NAN;

void setup()
{
   Wire.begin();
   Serial.begin(115200);
   feather.begin();

   while (!sensor.init())
   {
      feather.setCursor(0, 0);
      feather.setTextSize(2);
      feather.println("MS5837 init failed", Color::RED);
      delay(1000);
   }

   sensor.setModel(MS5837::MS5837_02BA);
   sensor.setFluidDensity(FRESH_WATER_DENSITY_KG_PER_M3);

   sensor.read();
   baselineDepthCm = sensor.depth() * METERS_TO_CENTIMETERS;
}

void loop()
{
   sensor.read();

   float currentDepthCm = sensor.depth() * METERS_TO_CENTIMETERS;
   float deltaDepthCm = currentDepthCm - baselineDepthCm;

   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Water Delta", Color::HEADING);

   feather.setTextSize(2);
   feather.println("Baseline: 0 cm", Color::LABEL);

   feather.setTextSize(5);
   feather.printlnC(deltaDepthCm, deltaDepthFormat, Color::VALUE);

   delay(DISPLAY_REFRESH_DELAY_MS);
}
