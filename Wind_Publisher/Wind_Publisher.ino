//
// Wind Publisher
//
// Reads wind speed from an anemometer and publishes live readings over a WebSocket
// telemetry connection, while also uploading rolling-averaged enclosure temperature/
// humidity and a point-in-time CPU temperature reading to InfluxDB on a fixed interval.
//
// The telemetry topic / InfluxDB site/location is one of the 3 entries in WIND_SITES.
// See Publisher.h for the shared init/loop sequence, site selection, and InfluxDB
// behavior.
//
// InfluxDB points uploaded (Measurement: Sensors, bucket=<selected>):
//
// - site=<selected>, location=<selected>, sensor=Wind, item=Enclosure
//     temperature: rolling average of the enclosure sensor's readTemperatureF(),
//     sampled every SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//     humidity: rolling average of the enclosure sensor's readHumidity(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//
// - site=<selected>, location=<selected>, sensor=Wind, item=CPU
//     temperature: the ESP32 CPU temperature at upload time.
//
// Note: wind speed is only streamed live over telemetry, not uploaded to InfluxDB.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Wind_Publisher";

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "WindMeter.h"
#include "WiFiSettings.h"

#include "Publisher.h"

// ----------- Telemetry topic / InfluxDB site selection
constexpr SiteConfig WIND_SITES[] = {
   { "Wind/Lake", "Monitor", "Lake", "Dock" },
   { "Wind/Bragg", "Monitor", "Bragg", "Studio" },
   { "Wind/Test", "Testing", "WindSite", "WindLocation" },
};

// ----------- Wind sensor pins
constexpr uint8_t WIND_SENSOR_PIN = 11;
constexpr uint8_t WIND_SENSOR_GROUND_PIN = 13; // held LOW to power the wind encoder/sensor
constexpr uint8_t WIND_SENSOR_POWER_PIN = 12; // held HIGH to power the wind encoder/sensor

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring.
Arduino arduino;
WindMeter wind(WIND_SENSOR_PIN, arduino.ledPin(), LEDColor::CLEAR_PINK);

PublisherConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = SKETCH_NAME,
   .sites = WIND_SITES,
   .influxSensor = "Wind",
   .includeEnclosureTemp = true,
   .includeCpuTemp = true,
   .enableOTA = true,
   .enableRebooter = true,
};

Publisher publisher(&arduino, PUBLISHER_CONFIG);

void setup()
{
   // power the wind encoder/sensor
   pinMode(WIND_SENSOR_GROUND_PIN, OUTPUT);
   pinMode(WIND_SENSOR_POWER_PIN, OUTPUT);
   digitalWrite(WIND_SENSOR_GROUND_PIN, LOW);
   digitalWrite(WIND_SENSOR_POWER_PIN, HIGH);

   publisher.addSensor("WindMeter", []() { wind.begin(); return true; });
   publisher.setValueSource([]() { return wind.getSpeed(); });

   // Turn off the status LED once telemetry finishes starting, since the sketch is
   // then fully up and running and no longer needs the LED for startup/connectivity
   // feedback.
   publisher.setOnStartedCallback([]() { arduino.setStatus(Status::NONE); });

   publisher.begin();
}

void loop()
{
   publisher.loop();
}

