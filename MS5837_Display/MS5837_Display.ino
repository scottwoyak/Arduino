#include "Feather.h"
#include "Stopwatch.h"
#include <MS5837.h>
#include <Units.h>
#include <RollingStats.h>


// 
// This sketch displays the current temperature on an Arduino ESP32 Feather
//
Feather feather;
MS5837 sensor;

Format tempFormat("###.## F");
Format depthFormat("##.## in");
Format altitudeFormat("####.# ft");
Format rateFormat("####/s");

constexpr auto NUM_SAMPLES = 20;
RollingStats temp(NUM_SAMPLES);
RollingStats depth(NUM_SAMPLES);
RollingStats altitude(NUM_SAMPLES);

// The setup() function runs once each time the micro-controller starts
void setup()
{
   Wire.begin();

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };

   feather.begin();

   while (!sensor.init())
   {
      Serial.println("Sensor failed to initialize");
      delay(1000);
   };

   sensor.setModel(MS5837::MS5837_02BA);
   //sensor.setModel(MS5837::MS5837_30BA);
   sensor.setFluidDensity(997); // kg/m^3 (fresh water)
}

Stopwatch sw;

void loop()
{
   feather.setCursor(0, 0);

   // read sensor
   sw.reset();
   sensor.read();
   temp.set(Units::C2F(sensor.temperature()));
   depth.set(Units::M2IN(sensor.depth()));
   altitude.set(Units::M2FT(sensor.altitude()));

   float time = sw.elapsedMillis();
   int16_t rate = round(1000 / time);

   // display values
   feather.setTextSize(3);
   feather.print("Temp: ", Color::LABEL);
   feather.println(temp.get(), tempFormat, Color::VALUE);
   feather.print("Depth: ", Color::LABEL);
   feather.println(depth.get(), depthFormat, Color::VALUE);
   feather.print("Alt: ", Color::LABEL);
   feather.println(altitude.get(), altitudeFormat, Color::VALUE);

   // display timing
   feather.setTextSize(2);
   feather.setCursor(0, -feather.charH() + 1);
   feather.print("Rate: ", Color::GRAY);
   feather.println(rate, rateFormat, Color::GRAY);
}


