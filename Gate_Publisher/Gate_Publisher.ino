//
// Gate Publisher
//
// Reads the compass azimuth from a QMC5883P magnetometer (as found on HW-127/GY-273
// breakouts) and publishes live readings over a WebSocket telemetry connection, while
// also uploading a rolling-averaged CPU temperature reading to InfluxDB on a fixed
// interval.
//
// The site is always "Bragg", but there are two gate locations: Left and Right. See
// Publisher.h for the shared init/loop sequence, site selection, and InfluxDB behavior.
//
// InfluxDB points uploaded (Measurement: Sensors, bucket=<selected>):
//
// - site=Bragg, location=<selected>, sensor=Gate, item=CPU
//     temperature: rolling average of cpuTemp.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//
// Note: azimuth is only streamed live over telemetry, not uploaded to InfluxDB.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Gate_Publisher";

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "QMC5883PMagnometer.h"
#include "WiFiSettings.h"

#include "Publisher.h"

// ----------- Telemetry topic / InfluxDB location selection
constexpr SiteConfig GATE_LOCATIONS[] = {
   { "Gate/Left", "Monitor", "Bragg", "Left" },
   { "Gate/Right", "Monitor", "Bragg", "Right" },
   { "Gate/Test", "Testing", "GateSite", "GateLocation" },
};

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring.
Arduino arduino;
QMC5883PMagnometer magnetometer;

PublisherConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = SKETCH_NAME,
   .sites = GATE_LOCATIONS,
   .influxSensor = "Gate",
   .includeEnclosureTemp = false,
   .includeCpuTemp = true,
   .enableOTA = true,
   .enableRebooter = true,
};

Publisher publisher(&arduino, PUBLISHER_CONFIG);

void setup()
{
   publisher.addSensor("QMC5883P", []() { return magnetometer.begin(); });
   publisher.setValueSource([]() { magnetometer.read(); return magnetometer.azimuth(); });

   publisher.begin();
}

void loop()
{
   publisher.loop();
}
