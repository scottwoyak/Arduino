/// <summary>
/// Wind speed monitoring station with percentile analysis and InfluxDB logging.
/// </summary>
/// <remarks>
/// Measures wind speed over configurable collection intervals, bins the data into
/// speed ranges, and calculates percentiles (10th, 30th, 50th, 70th, 90th) for analysis.
/// Computes gust metrics as the difference between 90th and 10th percentiles.
/// Logs measurements to InfluxDB at regular intervals.
/// 
/// Hardware: ESP32 with anemometer input, NeoPixel status LED.
/// </remarks>

#include <Arduino.h>
#include <cmath>

#include "ESP32TempSensor.h"
#include "Influx.h"
#include "SerialX.h"
#include "Status.h"
#include "Timer.h"
#include "TimedBin.h"
#include "WindMeter.h"

#include "WiFiSettings.h"

// Wind sensor setup
constexpr auto WIND_SENSOR_PIN = 1;
constexpr auto MAX_WIND_SPEED = 60;       // mph
constexpr auto WIND_SPEED_INCREMENT = 0.1f;  // Speed bin resolution

// Data collection timing
constexpr auto BIN_DURATION_MS = 3 * 60 * 1000;        // Bin duration (3 minutes)
constexpr auto DATA_COLLECTION_INTERVAL_MS = 100;      // Sampling interval
constexpr auto INFLUX_UPLOAD_INTERVAL_S = 15;          // Upload to InfluxDB every N seconds

// InfluxDB settings
constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr uint16_t NUM_SPEED_BINS = (MAX_WIND_SPEED / WIND_SPEED_INCREMENT);
constexpr auto WIFI_RESET_DELAY_S = 60;

WindMeter wind(WIND_SENSOR_PIN, 0);
ESP32TempSensor cpuTemp;
NeoPixelStatus status;

// Speed distribution bins for percentile calculation
TimedBin* speedBins[NUM_SPEED_BINS];
Timer binCaptureTimer(DATA_COLLECTION_INTERVAL_MS);

// InfluxDB client and data point
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Influx influx(WIFI_SSID, WIFI_PASSWORD, &client, &status);
Timer uploadTimer(1000 * INFLUX_UPLOAD_INTERVAL_S);
InfluxPoint point(INFLUX_MEASUREMENT);

// Field references for InfluxDB data
InfluxField* wind10Field = point.addValueField("wind10", 1);   // 10th percentile
InfluxField* wind30Field = point.addValueField("wind30", 1);   // 30th percentile
InfluxField* wind50Field = point.addValueField("wind50", 1);   // 50th percentile (median)
InfluxField* wind70Field = point.addValueField("wind70", 1);   // 70th percentile
InfluxField* wind90Field = point.addValueField("wind90", 1);   // 90th percentile
InfluxField* gustsField = point.addValueField("gusts", 1);     // Gust metric (90th - 10th)
InfluxField* cpuTempField = point.addValueField("cpuTemperature", 1);  // CPU temperature

void setup()
{
   SerialX::begin();
   Serial.println("Wind Monitor - Initializing");

   // Create speed distribution bins
   for (uint16_t i = 0; i < NUM_SPEED_BINS; i++)
   {
      speedBins[i] = new TimedBin(BIN_DURATION_MS);
   }

   status.begin();
   if (!influx.begin())
   {
      Util::reset(WIFI_RESET_DELAY_S);
   }
   status.setStatus(Status::STARTED);

   wind.begin();

   // Reduce CPU frequency for lower power consumption
   setCpuFrequencyMhz(80);

   Serial.println("Wind Monitor - Ready");
}

/// <summary>
/// Calculates the wind speed at the specified percentile.
/// </summary>
/// <param name="targetPercentile">Percentile to calculate (0.0 - 1.0, where 0.5 is median)</param>
/// <returns>Wind speed in mph at the given percentile</returns>
/// <remarks>
/// Iterates through speed bins, accumulating counts until the target percentile is reached.
/// Returns 0 if no data available.
/// </remarks>
float getValueForPercentile(float targetPercentile)
{
   uint32_t totalPoints = 0;
   for (uint16_t i = 0; i < NUM_SPEED_BINS; i++)
   {
      totalPoints += speedBins[i]->getCount();
   }

   // Handle empty data
   if (totalPoints == 0)
   {
      return 0.0f;
   }

   uint32_t points = 0;
   for (uint16_t i = 0; i < NUM_SPEED_BINS; i++)
   {
      float nextPercent = static_cast<float>(points + speedBins[i]->getCount()) / totalPoints;

      if (nextPercent >= targetPercentile)
      {
         return i * WIND_SPEED_INCREMENT;
      }

      points += speedBins[i]->getCount();
   }

   return 0.0f;
}

void loop()
{
   // Update LED status based on wind sensor activity
   // Note: Must be done in loop (not interrupt) to avoid NeoPixel driver crashes
   if (wind.ledState())
   {
      status.setStatus(1.0f, 0.0f, 0.0f);  // Red LED for active measurement
   }
   else
   {
      status.setStatus(Status::READY);
   }

   // Small delay to prevent watchdog timeout
   delay(1);

   // Collect wind speed samples at regular intervals
   if (binCaptureTimer.ready())
   {
      float speed = wind.getSpeed();
      uint16_t index = static_cast<uint16_t>(speed * 10);
      index = min(index, static_cast<uint16_t>(NUM_SPEED_BINS - 1));
      speedBins[index]->add();
   }

   // Calculate percentiles and upload to InfluxDB at configured interval
   if (uploadTimer.ready())
   {
      // Calculate wind speed percentiles
      wind10Field->set(getValueForPercentile(0.1f));
      wind30Field->set(getValueForPercentile(0.3f));
      wind50Field->set(getValueForPercentile(0.5f));
      wind70Field->set(getValueForPercentile(0.7f));
      wind90Field->set(getValueForPercentile(0.9f));

      // Calculate gust metric (difference between 90th and 10th percentiles)
      gustsField->set(wind90Field->get() - wind10Field->get());

      // Read and log CPU temperature
      cpuTempField->set(cpuTemp.readTemperatureF());

      // Ensure WiFi connectivity before upload
      if (!influx.ensureWiFiConnected())
      {
         Serial.println("WiFi reconnection failed, resetting");
         Util::reset(WIFI_RESET_DELAY_S);
      }

      // Upload data point to InfluxDB
      status.setStatus(1.0f, 0.5f, 0.0f);  // Yellow LED during upload

      if (!point.post(&client, true))
      {
         Serial.println("InfluxDB upload failed, resetting");
         Util::reset(WIFI_RESET_DELAY_S);
      }

      status.setStatus(Status::READY);
   }
}
