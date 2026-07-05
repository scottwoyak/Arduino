#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include <MS5837.h>
#include "Units.h"
#include "Rate.h"
#include "RollingStats.h"

//
// This sketch displays temperature, depth, and altitude from an MS5837 sensor.
//
Arduino arduino;
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

   arduino.begin();

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
   arduino.setCursor(0, 0);

   // read sensor
   rate.start();
   sensor.read();
   rate.stop();
   temp.set(Units::C2F(sensor.temperature()));
   depth.set(Units::M2IN(sensor.depth()));
   altitude.set(Units::M2FT(sensor.altitude()));

   // display values
   arduino.setTextSize(3);
   arduino.println("Temp: ", temp.get(), tempFormat);
   arduino.println("Depth: ", depth.get(), depthFormat);
   arduino.println("Alt: ", altitude.get(), altitudeFormat);

   // display timing
   arduino.setTextSize(2);
   arduino.setCursor(0, -arduino.charH() + 1);
   arduino.print("Rate: ", Color::GRAY);
   arduino.println(rate.get(), rateFormat, Color::GRAY);
}


