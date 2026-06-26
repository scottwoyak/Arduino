#include "Feather.h"
#include <MS5837.h>
#include "Units.h"
#include "Rate.h"
#include "RollingStats.h"

//
// This sketch displays temperature, depth, and altitude from an MS5837 sensor.
//
Feather feather;
MS5837 sensor;

Format tempFormat("###.## F");
Format depthFormat("##.## in");
Format altitudeFormat("####.# ft");
Format rateFormat("####/s");

constexpr uint16_t NUM_SAMPLES = 20;
RollingStats temp(NUM_SAMPLES);
RollingStats depth(NUM_SAMPLES);
RollingStats altitude(NUM_SAMPLES);
Rate rate;

// The setup() function runs once each time the micro-controller starts
void setup()
{
   Wire.begin();

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   }

   feather.begin();

   while (!sensor.init())
   {
      Serial.println("Sensor failed to initialize");
      delay(1000);
   }

   sensor.setModel(MS5837::MS5837_02BA);
   //sensor.setModel(MS5837::MS5837_30BA);
   sensor.setFluidDensity(997); // kg/m^3 (fresh water)
}

void loop()
{
   feather.setCursor(0, 0);

   // read sensor
   rate.start();
   sensor.read();
   rate.stop();
   temp.set(Units::C2F(sensor.temperature()));
   depth.set(Units::M2IN(sensor.depth()));
   altitude.set(Units::M2FT(sensor.altitude()));

   // display values
   feather.setTextSize(3);
   feather.println("Temp: ", temp.get(), tempFormat);
   feather.println("Depth: ", depth.get(), depthFormat);
   feather.println("Alt: ", altitude.get(), altitudeFormat);

   // display timing
   feather.setTextSize(2);
   feather.setCursor(0, -feather.charH() + 1);
   feather.print("Rate: ", Color::GRAY);
   feather.println(rate.get(), rateFormat, Color::GRAY);
}


