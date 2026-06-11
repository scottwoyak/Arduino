
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
#include <ESP32TempSensor.h>

WiFiMulti wifi;

constexpr uint8_t NUM_DECIMALS = 2;

constexpr uint8_t WIND_PIN = 1;
WindMeter wind(WIND_PIN, 0);
ESP32TempSensor cpuTemp;

NeoPixelStatus status;

constexpr auto BIN_DURATION_MS = 3* 60 * 1000;
constexpr auto INFLUX_UPLOAD_INTERVAL_S = 15;
constexpr auto DATA_COLLECTION_INTERVAL_MS = 100;

constexpr auto MAX_SPEED = 60;
constexpr auto SPEED_INCREMENTS = 0.1;
constexpr uint NUM_BINS = (MAX_SPEED / SPEED_INCREMENTS);

TimedBin* speedBins[NUM_BINS];
Timer binCaptureTimer(DATA_COLLECTION_INTERVAL_MS);

constexpr auto INFLUX_MEASUREMENT = "Air";
Timer uploadTimer(1000 * INFLUX_UPLOAD_INTERVAL_S);
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
SimplePoint point(INFLUX_MEASUREMENT); // Influx data point
Field* wind10Field = point.addValueField("wind10", 1);
Field* wind30Field = point.addValueField("wind30", 1);
Field* wind50Field = point.addValueField("wind50", 1);
Field* wind70Field = point.addValueField("wind70", 1);
Field* wind90Field = point.addValueField("wind90", 1);
Field* gustsField = point.addValueField("gusts", 1);
Field* cpuTempField = point.addValueField("cpuTemperature", 1);

void setup()
{
   for (int i = 0; i < NUM_BINS; i++)
   {
	  speedBins[i] = new TimedBin(SAMPLING_DURATION_MS);
   }

   status.begin();

   Influx::begin(WIFI_SSID, WIFI_PASSWORD, &client, &status);

   status.setStatus(Status::STARTED);

   SerialX::begin();
   Serial.println("Wind Monitor");

   wind.begin();

   setCpuFrequencyMhz(80); // keep things cool  
}

float getValueForPercentile(float targetPercentile)
{
   // TODO cache this value
   uint totalPoints = 0;
   for (int i = 0; i < NUM_BINS; i++)
   {
      totalPoints += speedBins[i]->getCount();
   }

   uint points = 0;
   for (int i = 0; i < NUM_BINS; i++)
   {
      float nextPercent = ((float)(points + speedBins[i]->getCount())) / totalPoints;

      if (nextPercent >= targetPercentile)
      {
         return i / 10.0f;
      }
      points += speedBins[i]->getCount();
   }
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

   if (binCaptureTimer.ready())
   {
      float speed = wind.getSpeed();
      uint index = std::min((uint)(10*speed), NUM_BINS - 1);
      speedBins[index]->add();
   }

   // Write point
   if (uploadTimer.ready())
   {
      wind10Field->set(getValueForPercentile(0.1));
      wind30Field->set(getValueForPercentile(0.3));
      wind50Field->set(getValueForPercentile(0.5));
      wind70Field->set(getValueForPercentile(0.7));
      wind90Field->set(getValueForPercentile(0.9));
      gustsField->set(wind90Field->get() - wind10Field->get());
      cpuTempField->set(cpuTemp.readTemperatureF());


      status.setStatus(1.0f, 0.5f, 0);
      if (point.post(&client, true) == false)
      {
         Util::reset(60);
      }
      status.setStatus(Status::READY);
   }
}