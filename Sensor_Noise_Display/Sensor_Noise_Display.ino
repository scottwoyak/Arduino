#include "Feather.h"
#include "Stopwatch.h"
#include "SerialX.h"
#include "TempSensor.h"
#include "RollingStdDev.h"
#include "I2C.h"

// 
// This sketch estimates how many samples are needed to meet error tolerances
// based on observed sensor noise.
//
Feather feather;
TempSensor sensor;
Stopwatch sw;
RollingStdDev values(1000);

Format valueFormat("###.##");
Format sigmaFormat("###.###");
Format toleranceFormat("#.##");

constexpr float Z_SCORE_95 = 1.96f;
constexpr float TOLERANCES[] = 
{ 
   1.00f,
   0.50f, 
   0.10f, 
   0.05f,
   0.01f
};
constexpr uint8_t NUM_TOLERANCES = sizeof(TOLERANCES) / sizeof(TOLERANCES[0]);

size_t requiredSamples(float sigma, float tolerance)
{
   if (!std::isfinite(sigma) || tolerance <= 0)
   {
      return 0;
   }

   float n = (Z_SCORE_95 * sigma) / tolerance;
   n *= n;
   return (size_t)ceilf(n);
}

void setup()
{
   Wire.begin();
   SerialX::begin(115200, 1000);
   feather.begin();

   sensor.begin();
}

void loop()
{
   sw.reset();
   float temp = sensor.readTemperatureF();
   float elapsedMs = sw.elapsedMillis();
   int16_t samplesPerSecond = elapsedMs > 0 ? (int16_t)round(1000.0f / elapsedMs) : 0;

   values.set(temp);

   float avg = values.mean();
   float sigma = values.get();

   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Noise Calculations", Color::HEADING);
   feather.moveCursorY(5);
   uint16_t y = feather.getCursorY();

   feather.setTextSize(2);
   feather.print(" Value ", Color::LABEL);
   feather.println(temp, valueFormat, Color::VALUE);

   feather.print("   Avg ", Color::LABEL);
   feather.println(avg, valueFormat, Color::VALUE);

   feather.print("StdDev ", Color::LABEL);
   feather.println(sigma, sigmaFormat, Color::VALUE2);

   feather.print("    Hz ", Color::LABEL);
   feather.println(samplesPerSecond, Color::VALUE2);

   uint16_t x = feather.width() / 2 + feather.charW();
   feather.setCursor(x, y);
   for (uint8_t i = 0; i < NUM_TOLERANCES; i++)
   {
      feather.setCursorX(x);
      float tolerance = TOLERANCES[i];
      size_t n = requiredSamples(sigma, tolerance);

      feather.print(tolerance, toleranceFormat, Color::LABEL);
      feather.print(": n=", Color::LABEL);
      if (n == 0)
      {
         feather.println("--", Color::GRAY);
      }
      else
      {
         feather.println((uint32_t)n, Color::VALUE);
      }
   }
}
