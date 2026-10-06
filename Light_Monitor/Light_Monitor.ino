//
// Light Monitor
//
// Uses a VL53L1X time-of-flight sensor to detect motion (the presence of people) and a
// VEML7700 sensor to measure the ambient light level. Presence is anything detected
// within PRESENCE_DISTANCE_MM (3 feet). The sensors are sampled once per second.
//
// Every INFLUX_INTERVAL_S seconds the following are uploaded to InfluxDB (standard bucket;
// the site is always Bragg and the location is selected by the standard serial prompt):
// - lux: average light level over the interval
// - activity: percent of the interval where the distance reading was less than
//   PRESENCE_DISTANCE_MM
//
// The raw readings are streamed live over WebSocket telemetry:
// - Distance/<site>/<location>:
// - Lux/<site>/<location>:
//
// Both topics are published over the single PublisherSketch telemetry connection.
// See PublisherSketch.h for the shared init/loop sequence.
//
// Requires the Adafruit_VL53L1X and Adafruit_VEML7700_Library libraries.
//

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include <Arduino.h>
#include <Wire.h>

#include <Adafruit_VL53L1X.h>

#include "ArduinoBoard.h"
#include "LibraryVersion.h"
#include "Timer.h"
#include "VEML7700LightSensor.h"
#include "WiFiSettings.h"

#include "PublisherSketch.h"

// This sketch's own version (e.g. "1.0"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Light_Monitor";

// ----------- Telemetry
constexpr auto DISTANCE_TOPIC_PREFIX = "Distance";
constexpr auto LUX_TOPIC_PREFIX = "Lux";
constexpr size_t MAX_TOPIC_LENGTH = 64;
constexpr uint8_t LUX_DECIMALS = 1;
constexpr uint16_t DISTANCE_PUBLISH_INTERVAL_MS = 100;
constexpr float RECONNECT_LOG_INTERVAL_S = 300.0f;

// ----------- Sampling and InfluxDB
constexpr uint16_t SAMPLE_INTERVAL_MS = 1000;
constexpr uint16_t INFLUX_INTERVAL_S = 15;
constexpr uint8_t INFLUX_LUX_DECIMALS = 1;
constexpr uint8_t INFLUX_PRESENCE_DECIMALS = 1;

// Only the location is prompted for; the bucket is the standard INFLUXDB_BUCKET and the site is fixed to Bragg (see INFLUX_CONFIG).

// ----------- Presence detection
constexpr uint8_t VL53L1X_I2C_ADDRESS = 0x29;
constexpr int16_t PRESENCE_DISTANCE_MM = 914; // 3 feet

Adafruit_VL53L1X distanceSensor;
VEML7700LightSensor luxSensor;

int16_t distanceMm = -1;
float lux = 0.0f;
bool presentSinceSample = false;
Timer sampleTimer(SAMPLE_INTERVAL_MS);

// Registered after begin(), once the site has been resolved.
InfluxField* luxField = nullptr;
InfluxField* presenceField = nullptr;

// Topic buffers, filled in once the site and location are known (see onSiteResolved()).
char distanceTopic[MAX_TOPIC_LENGTH] = "";
char luxTopic[MAX_TOPIC_LENGTH] = "";

///
/// <summary>
/// Builds the telemetry topics as "Distance/site/location" and "Lux/site/location".
/// </summary>
/// <param name="site">The resolved InfluxDB site.</param>
///
void onSiteResolved(const InfluxContext& site)
{
   std::string suffix = std::string("/") + site.site + "/" + site.location;
   strlcpy(distanceTopic, (std::string(DISTANCE_TOPIC_PREFIX) + suffix).c_str(), sizeof(distanceTopic));
   strlcpy(luxTopic, (std::string(LUX_TOPIC_PREFIX) + suffix).c_str(), sizeof(luxTopic));
}

///
/// <summary>
/// Gets the most recent distance measurement, for publishing over telemetry. The sensor
/// itself is only read once per SAMPLE_INTERVAL_MS (see sampleSensors()).
/// </summary>
/// <returns>Distance in millimeters, or -1 if nothing is in range.</returns>
///
float readDistance()
{
   return distanceMm;
}

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = distanceTopic,
   .decimals = 0,
   .publishIntervalMs = DISTANCE_PUBLISH_INTERVAL_MS,
};

InfluxConfig INFLUX_CONFIG = {
   .context = { INFLUXDB_BUCKET, "Bragg", nullptr },
   .intervalS = INFLUX_INTERVAL_S,
   .sampleIntervalMs = SAMPLE_INTERVAL_MS,
   .promptForContext = true,
   .includeEnclosureTemp = false,
   .includeCpuTemp = false,
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = "LightMon",
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

PublisherSketch sketch(PUBLISHER_CONFIG, INFLUX_CONFIG, TELEMETRY_CONFIG);

///
/// <summary>
/// Adds the current distance, light level, and light state to a GetStatus reply, on top
/// of Logger's/SketchBase's base fields.
/// </summary>
/// <param name="status">The in-progress status to add fields to.</param>
///
void onStatus(LoggerStatus& status)
{
   status.add("Distance", (float)distanceMm, 0);
   status.add("Lux", lux, LUX_DECIMALS);
}

///
/// <summary>
/// Gets whether the most recent distance reading is within PRESENCE_DISTANCE_MM. A failed
/// reading (-1) means nothing is in range.
/// </summary>
/// <returns>True if someone is present.</returns>
///
bool isPresent()
{
   return distanceMm >= 0 && distanceMm < PRESENCE_DISTANCE_MM;
}

///
/// <summary>
/// Reads both sensors, and records the readings for telemetry and InfluxDB. Presence is
/// recorded as 100 (present) or 0 so that its average is the percent of time present.
/// </summary>
///
void sampleSensors()
{
   lux = luxSensor.readLux();
   sketch.publish(luxTopic, lux, LUX_DECIMALS);

   luxField->set(lux);
   presenceField->set(presentSinceSample ? 100.0f : 0.0f);
   presentSinceSample = false;
}

///
/// <summary>
/// Reads every new distance measurement as it becomes available, and latches whether
/// anyone was detected since the last 1 Hz sample so brief presence isn't missed.
/// </summary>
///
void pollDistance()
{
   if (distanceSensor.dataReady())
   {
      distanceMm = distanceSensor.distance();
      distanceSensor.clearInterrupt();

      if (isPresent())
      {
         presentSinceSample = true;
      }
   }
}

void setup()
{
   // Adafruit_VL53L1X::begin() retries its I2C reads forever if the sensor doesn't respond,
   // so check that it's present first instead of hanging
   sketch.addSensor("VL53L1X", []() {
      Wire.beginTransmission(VL53L1X_I2C_ADDRESS);
      return Wire.endTransmission() == 0 && distanceSensor.begin() && distanceSensor.startRanging();
   });
   sketch.addSensor("VEML7700", []() { return luxSensor.begin(); });
   sketch.setValueSource(readDistance);
   sketch.onSiteResolved(onSiteResolved);

   sketch.begin();
   sketch.onStatus(onStatus);

   InfluxPoint* point = sketch.addPoint();
   luxField = point->addTimeAverageField(INFLUX_INTERVAL_S, "lux", INFLUX_LUX_DECIMALS);
   presenceField = point->addTimeAverageField(INFLUX_INTERVAL_S, "activity", INFLUX_PRESENCE_DECIMALS);

   Logger.logInitializationComplete();
}

void loop()
{
   sketch.loop();
   pollDistance();

   if (sampleTimer.ready())
   {
      sampleSensors();
   }
}
