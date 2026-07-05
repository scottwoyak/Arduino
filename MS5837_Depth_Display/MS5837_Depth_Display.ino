/// <summary>
/// Displays MS5837 water depth delta in centimeters relative to startup level.
/// </summary>
/// <remarks>
/// Initializes the I2C bus, Feather display, and MS5837 pressure sensor, retrying sensor
/// initialization until successful and showing a visible error message on display in the meantime.
/// Uses fresh-water density for depth conversion and stores startup depth as the zero reference,
/// then continuously reads depth and displays delta depth (currentDepthCm - baselineDepthCm).
/// 
/// Display output: the title row reads "Water Delta", the baseline row shows the fixed
/// "Baseline: 0 cm" reference, and the main value row shows the signed depth delta in centimeters.
/// 
/// Typical usage: place the sensor at the reference water level, then power/reset the board so the
/// startup reading becomes 0 cm; observe positive/negative delta values as the water level moves
/// from that reference.
/// </remarks>

#include <Arduino.h>
#include <Wire.h>
#include <MS5837.h>
#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

Arduino arduino;
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
   arduino.begin();

   while (!sensor.init())
   {
      arduino.setCursor(0, 0);
      arduino.setTextSize(2);
      arduino.println("MS5837 init failed", Color::RED);
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

   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println("Water Delta", Color::HEADING);

   arduino.setTextSize(2);
   arduino.println("Baseline: 0 cm", Color::LABEL);

   arduino.setTextSize(5);
   arduino.printlnC(deltaDepthCm, deltaDepthFormat, Color::VALUE);

   delay(DISPLAY_REFRESH_DELAY_MS);
}
