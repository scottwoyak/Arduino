//
// Wave Publisher
//
// Reads water depth from an ultrasonic or MS5837 pressure sensor and publishes live
// wave height readings over a WebSocket telemetry connection. The sensor type is
// prompted for at startup and remembered (see telemetry topic selection below).
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   streams live raw depth readings as they're read; the subscribing client is
//   responsible for computing wave height from a running average of these readings.
// - Drives the general-purpose LED at full brightness while starting up, then switches
//   to a brightness proportional to wave height once telemetry is connected: off at
//   or below LED_WAVE_HEIGHT_LOW_CM, full at or above LED_WAVE_HEIGHT_HIGH_CM, and
//   linearly interpolated in between. Updated every DEPTH_SAMPLE_INTERVAL_MS.
// - Posts the average depth, enclosure temperature/humidity, and CPU temperature to
//   InfluxDB every INFLUX_INTERVAL_S seconds.
// - Restarts the device every 24 hours to play it safe, and on telemetry disconnect
//   or error.
// - Checks for a firmware update periodically.
//
// The telemetry topic selection (WAVE_TELEMETRY_TOPICS) doubles as the sensor type
// selection: Waves/Ultrasonic uses the ultrasonic sensor, Waves/Pressure uses the
// MS5837 pressure sensor. The InfluxDB site/location is chosen independently from
// the 2 entries in INFLUX_PROMPTS.
// See PublisherSketch.h for the shared init/loop sequence, site selection, and InfluxDB
// behavior.
//
// InfluxDB points uploaded (Measurement: Sensors, bucket=<selected>):
//
// - site=<selected>, location=<selected>
//     avgDepth: rolling average of raw depth readings (depth->getDepth()) over the
//     5 minute averaging window (DepthSensorBase::DEFAULT_AVERAGE_DURATION_M); only
//     posted once that window is fully populated (depth->isAverageFull()).
//
// - site=<selected>, location=<selected>, item=Enclosure
//     temperature: rolling average of the enclosure sensor's readTemperatureF(),
//     sampled every SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//     humidity: rolling average of the enclosure sensor's readHumidity(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//
// - site=<selected>, location=<selected>, item=CPU
//     temperature: the ESP32 CPU temperature at upload time.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include <cstring>

#include "ArduinoBoard.h"
#include "DepthSensorBase.h"
#include "LibraryVersion.h"
#include "Timer.h"
#include "WiFiSettings.h"

#include "PublisherSketch.h"

// This sketch's own version (e.g. "v1.0"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("v1.0");
constexpr auto SKETCH_NAME = "Wave_Publisher";

#include "UltrasonicDepthSensor.h"
#include "MS5837DepthSensor.h"

// ----------- Ultrasonic sensor pins
constexpr uint8_t TRIGGER_PIN = 10;
constexpr uint8_t ECHO_PIN = 11;

// ----------- InfluxDB site selection
constexpr InfluxContext INFLUX_PROMPTS[] = {
   { "Monitor", "Lake", "Dock" },
   { "Testing", "Lake", "Dock" },
};

// ----------- Telemetry topic selection; also determines which physical depth sensor
// is used (see the sensor init lambda in setup()).
constexpr auto TOPIC_ULTRASONIC = "Waves/Ultrasonic";
constexpr auto TOPIC_PRESSURE = "Waves/Pressure";

constexpr const char* WAVE_TELEMETRY_TOPICS[] = {
   TOPIC_ULTRASONIC,
   TOPIC_PRESSURE,
};

constexpr uint8_t INFLUX_AVG_DEPTH_DECIMALS = 2;

// ----------- LED wave height indicator
// The general-purpose LED (arduino.led) is dimmed to reflect the current wave height:
// off at/below LED_WAVE_HEIGHT_LOW_CM, full at/above LED_WAVE_HEIGHT_HIGH_CM, and
// linearly interpolated in between.
constexpr float LED_WAVE_HEIGHT_LOW_CM = -10.0f;
constexpr float LED_WAVE_HEIGHT_HIGH_CM = 10.0f;

constexpr uint16_t DEPTH_SAMPLE_INTERVAL_MS = 100;

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring. arduino itself implements IStatus and drives both the
// external RGB LED and the onboard NeoPixel, so status is visible even when the
// external LED isn't plugged in.
Arduino arduino;

// Allocated in setup(), once the telemetry topic (and thus the sensor type) has been resolved.
DepthSensorBase* depth = nullptr;

InfluxConfig INFLUX_CONFIG = {
   .prompts = INFLUX_PROMPTS,
   .includeEnclosureTemp = true,
   .includeCpuTemp = true,
};

TelemetryConfig TELEMETRY_CONFIG = {
   .prompts = WAVE_TELEMETRY_TOPICS,
   .decimals = 1,
   .publishIntervalMs = 33, // 30 per sec
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = SKETCH_NAME,
   .enableOTA = true,
   .enableRebooter = true,
};

PublisherSketch publisher(&arduino, PUBLISHER_CONFIG, INFLUX_CONFIG, TELEMETRY_CONFIG);

// Registered after begin(), once the site has been resolved.
InfluxField* averageDepthField = nullptr;

Timer depthSampleTimer(DEPTH_SAMPLE_INTERVAL_MS);

///
/// <summary>
/// Adds the current average depth and wave height readings to a GetStatus reply, on
/// top of Logger's/SketchBase's base fields.
/// </summary>
/// <param name="status">The in-progress status to add fields to.</param>
///
void onStatus(LoggerStatus& status)
{
   status.add("Average Depth", depth->getAverageDepth(), INFLUX_AVG_DEPTH_DECIMALS);
   status.add("Wave Height", depth->getWaveHeight(), INFLUX_AVG_DEPTH_DECIMALS);
}

void setup()
{
   // solid on while starting up; switches to wave-height-based fading in loop() once wave data is available
   arduino.led.turnOn(1.0f);

   // The telemetry topic (and thus the sensor type) is resolved by publisher.begin()
   // before this lambda runs, so only the sensor that's actually wired is constructed.
   publisher.addSensor("Depth Sensor", []() {
      if (strcmp(publisher.telemetryTopic(), TOPIC_ULTRASONIC) == 0)
      {
         depth = new UltrasonicDepthSensor(TRIGGER_PIN, ECHO_PIN);
      }
      else
      {
         depth = new MS5837DepthSensor();
      }
      return depth->begin();
   });
   publisher.setValueSource([]() { return depth->getDepth(); });

   publisher.begin();
   publisher.onStatus(onStatus);

   // avgDepth isn't posted until the 5 minute averaging window is full (see loop())
   InfluxPoint* devicePoint = publisher.addPoint("Sensors", {});
   averageDepthField = devicePoint->addValueField("avgDepth", INFLUX_AVG_DEPTH_DECIMALS);
   averageDepthField->setEnabled(false);

   Logger.logInitializationComplete();
}

void loop()
{
   publisher.loop();

   if (depthSampleTimer.ready())
   {
      float waveHeightCM = depth->getWaveHeight();
      float ledLevel = (waveHeightCM - LED_WAVE_HEIGHT_LOW_CM) / (LED_WAVE_HEIGHT_HIGH_CM - LED_WAVE_HEIGHT_LOW_CM);
      arduino.led.setLevel(constrain(ledLevel, 0.0f, 1.0f));

      // avgDepth isn't posted until the 5 minute averaging window is full, so it doesn't
      // report a partially-averaged value while the enclosure/CPU fields are already
      // posting on their own 60 second cadence.
      if (depth->isAverageFull())
      {
         averageDepthField->setEnabled(true);
         averageDepthField->set(depth->getAverageDepth());
      }
   }
}
