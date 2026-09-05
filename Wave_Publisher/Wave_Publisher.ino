//
// Wave Publisher
//
// Reads water depth from an ultrasonic (or MS5837 pressure) sensor and publishes live
// wave height readings over a WebSocket telemetry connection.
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
// The telemetry topic / InfluxDB site/location is one of the 2 entries in WAVE_SITES.
// See Publisher.h for the shared init/loop sequence, site selection, and InfluxDB
// behavior.
//
// InfluxDB points uploaded (Measurement: Sensors, bucket=<selected>):
//
// - site=<selected>, location=<selected>, sensor=WaveHeight
//     avgDepth: rolling average of raw depth readings (depth->getDepth()) over the
//     5 minute averaging window (DepthSensorBase::DEFAULT_AVERAGE_DURATION_M); only
//     posted once that window is fully populated (depth->isAverageFull()).
//
// - site=<selected>, location=<selected>, sensor=WaveHeight, item=Enclosure
//     temperature: rolling average of the enclosure sensor's readTemperatureF(),
//     sampled every SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//     humidity: rolling average of the enclosure sensor's readHumidity(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//
// - site=<selected>, location=<selected>, sensor=WaveHeight, item=CPU
//     temperature: the ESP32 CPU temperature at upload time.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Wave_Publisher";

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "DepthSensorBase.h"
#include "Timer.h"
#include "WiFiSettings.h"

#include "Publisher.h"

//#define USE_ULTRASONIC
#define USE_MS5837

#ifdef USE_ULTRASONIC
#include "UltrasonicDepthSensor.h"

// ----------- Ultrasonic sensor pins
constexpr uint8_t TRIGGER_PIN = 10;
constexpr uint8_t ECHO_PIN = 11;
#endif

#ifdef USE_MS5837
#include "MS5837DepthSensor.h"
#endif

// ----------- Telemetry topic / InfluxDB site selection
constexpr SiteConfig WAVE_SITES[] = {
   { "Waves/LakeP", "Sensors", "Lake", "Dock" },
   { "Waves/Test", "Testing", "WaveSite", "WaveLocation" },
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

#ifdef USE_ULTRASONIC
UltrasonicDepthSensor depthSensor(TRIGGER_PIN, ECHO_PIN);
#endif

#ifdef USE_MS5837
MS5837DepthSensor depthSensor;
#endif

DepthSensorBase* const depth = &depthSensor;

PublisherConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = SKETCH_NAME,
   .sites = WAVE_SITES,
   .influxSensor = "WaveHeight",
   .influxDecimals = 1,
   .telemetryDecimals = 1,
   .sensorIntervalMs = 5000,
   .publishIntervalMs = 33, // 30 per sec
   .includeEnclosureTemp = true,
   .includeCpuTemp = true,
};

Publisher publisher(&arduino, PUBLISHER_CONFIG);

// Registered after begin(), once the site has been resolved.
InfluxField* averageDepthField = nullptr;

Timer depthSampleTimer(DEPTH_SAMPLE_INTERVAL_MS);

void setup()
{
   // solid on while starting up; switches to wave-height-based fading in loop() once wave data is available
   arduino.led.turnOn(1.0f);

   publisher.addSensor("Depth Sensor", []() { return depth->begin(); });
   publisher.setValueSource([]() { return depth->getDepth(); });

   publisher.begin();

   // avgDepth isn't posted until the 5 minute averaging window is full (see loop())
   InfluxPoint* devicePoint = publisher.addPoint("Sensors", { { "sensor", "WaveHeight" } });
   averageDepthField = devicePoint->addValueField("avgDepth", INFLUX_AVG_DEPTH_DECIMALS);
   averageDepthField->setEnabled(false);
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
