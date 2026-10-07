//
// Wind Publisher
//
// Reads wind speed from an anemometer and publishes live readings over a WebSocket
// telemetry connection, while also uploading rolling-averaged enclosure temperature/
// humidity and a point-in-time CPU temperature reading to InfluxDB on a fixed interval.
//
// The telemetry topic / InfluxDB site/location is one of the 3 entries in WIND_SITES.
// See PublisherSketch.h for the shared init/loop sequence, site selection, and InfluxDB
// behavior.
//
// InfluxDB points uploaded (Measurement: Sensors, bucket=<selected>):
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
// Note: wind speed is only streamed live over telemetry, not uploaded to InfluxDB.
//

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "LibraryVersion.h"
#include "WindMeter.h"
#include "WiFiSettings.h"

#include "PublisherSketch.h"

// This sketch's own version (e.g. "1.2"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("1.3");
constexpr auto SKETCH_NAME = "Wind_Publisher";

// ----------- InfluxDB site selection
constexpr InfluxContext INFLUX_PROMPTS[] = {
   { "Telemetry_30_Day", "Lake", "Dock" },
   { "Telemetry_30_Day", "Bragg", "Studio" },
};

// ----------- Telemetry topic selection
constexpr const char* WIND_TELEMETRY_TOPICS[] = {
   "Wind/Lake",
   "Wind/Bragg",
   "Wind/Test",
};

// ----------- Wind sensor pins
constexpr uint8_t WIND_SENSOR_PIN = 11;
constexpr uint8_t WIND_SENSOR_GROUND_PIN = 13; // held LOW to power the wind encoder/sensor
constexpr uint8_t WIND_SENSOR_POWER_PIN = 12; // held HIGH to power the wind encoder/sensor

InfluxConfig INFLUX_CONFIG = {
   .prompts = INFLUX_PROMPTS,
   .includeEnclosureTemp = true,
   .includeCpuTemp = true,
};

// Caps publishing at 20 samples/sec rather than every loop() iteration, since faster
// updates aren't useful and would just add WiFi/WebSocket send overhead.
constexpr uint16_t TELEMETRY_PUBLISH_INTERVAL_MS = 1000 / 20;

TelemetryConfig TELEMETRY_CONFIG = {
   .prompts = WIND_TELEMETRY_TOPICS,
   .publishIntervalMs = TELEMETRY_PUBLISH_INTERVAL_MS,
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = SKETCH_NAME,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

PublisherSketch sketch(PUBLISHER_CONFIG, INFLUX_CONFIG, TELEMETRY_CONFIG);

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring.
WindMeter wind(WIND_SENSOR_PIN, sketch.arduino.ledPin(), LEDColor::CLEAR_PINK);

void setup()
{
   // power the wind encoder/sensor
   pinMode(WIND_SENSOR_GROUND_PIN, OUTPUT);
   pinMode(WIND_SENSOR_POWER_PIN, OUTPUT);
   digitalWrite(WIND_SENSOR_GROUND_PIN, LOW);
   digitalWrite(WIND_SENSOR_POWER_PIN, HIGH);

   sketch.addSensor("WindMeter", []() { wind.begin(); return true; });
   sketch.setValueSource([]() { return wind.getSpeed(); });

   // Turn off the status LED once telemetry finishes starting, since the sketch is
   // then fully up and running and no longer needs the LED for startup/connectivity
   // feedback.
   sketch.setOnStartedCallback([]() { sketch.arduino.off(); });

   sketch.begin();

   Logger.logInitializationComplete();
}

void loop()
{
   sketch.loop();
}

