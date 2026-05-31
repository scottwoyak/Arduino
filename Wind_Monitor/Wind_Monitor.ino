
//
// Wind Monitor
//

#include <WiFiMulti.h>
#include <SerialX.h>
#include <WiFiSettings.h>
#include <WindMeter.h>
#include <Status.h>
#include <Timer.h>
#include <TimedBin.h>
#include <Influx.h>

WiFiMulti wifi;

constexpr uint8_t NUM_DECIMALS = 2;

constexpr uint8_t WIND_PIN = 1;
WindMeter wind(WIND_PIN, 0);

NeoPixelStatus status;

constexpr auto DURATION_MS = 60 * 1000;
constexpr auto MAX_SPEED = 60;
constexpr auto SPEED_INCREMENTS = 0.1;
constexpr uint NUM_BINS = (MAX_SPEED / SPEED_INCREMENTS);

TimedBin* speedBins[NUM_BINS];
Timer timer(100);

constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
TimedPoint point(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT); // Influx data point
Field* wind10Field = point.addValueField("wind10", 1);
Field* wind30Field = point.addValueField("wind30", 1);
Field* wind50Field = point.addValueField("wind50", 1);
Field* wind70Field = point.addValueField("wind70", 1);
Field* wind90Field = point.addValueField("wind90", 1);

void setup()
{
   for (int i = 0; i < NUM_BINS; i++)
   {
	  speedBins[i] = new TimedBin(DURATION_MS);
   }

   status.begin();

   Influx::begin(WIFI_SSID, WIFI_PASSWORD, &client, &status);

   status.setStatus(Status::STARTED);

   SerialX::begin();
   Serial.println("Wind Monitor");

   wind.begin();

   setCpuFrequencyMhz(80); // keep things cool  
}


void loop()
{
   // Waveshare crashes if we set the LED value in the interrupt, so we manually do it
   if (wind.ledState())
   {
      status.setStatus(1.0f, 0, 0);
   }
   else
   {
      status.setStatus(Status::READY);
   }

   // without a delay, the waveshare crashes
   delay(1);

   if (timer.ready())
   {
      float speed = wind.getSpeed();
      uint index = std::min((uint)(10*speed), NUM_BINS - 1);
      speedBins[index]->add();

      Serial.println("Storing: " + String(speed) + ": " + String(index));
   }

   // Write point
   if (point.ready())
   {
      // compute all the metrics
      uint totalPoints = 0;
      for (int i = 0; i < NUM_BINS; i++)
      {
         totalPoints += speedBins[i]->getCount();
      }
      Serial.println("Total Points: " + String(totalPoints));

      uint points = 0;
      for (int i = 0; i < NUM_BINS; i++)
      {
         float currentPercent = ((float) points)/totalPoints;
         float nextPercent = ((float) (points + speedBins[i]->getCount())) / totalPoints;
         if (i < 150)
         {
            Serial.println(String(i) + " Current: " + String(currentPercent) + " Next: " + String(nextPercent));
         }

         if (currentPercent <= 0.1 && nextPercent > 0.1)
         {
            wind10Field->set(i/10.0f);
         }
         if (currentPercent <= 0.3 && nextPercent > 0.3)
         {
            wind30Field->set(i / 10.0f);
         }
         if (currentPercent <= 0.5 && nextPercent > 0.5)
         {
            wind50Field->set(i / 10.0f);
         }
         if (currentPercent <= 0.7 && nextPercent > 0.7)
         {
            wind70Field->set(i / 10.0f);
         }
         if (currentPercent <= 0.9 && nextPercent > 0.9)
         {
            wind90Field->set(i / 10.0f);
         }
         points += speedBins[i]->getCount();
      }

      status.setStatus(1.0f, 0.5f, 0);
      if (point.post(&client, true) == false)
      {
         Util::reset(60);
      }
      status.setStatus(Status::READY);
   }
}