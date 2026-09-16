//
// Reads live temperature and humidity from a single sensor and uploads averaged
// readings to InfluxDB on a fixed interval. This is the display-free counterpart to
// Temp_Monitor_Display, intended for a Waveshare ESP32-S3-Zero board with no display.
//
// Behavior:
// - Uses the shared Monitor class (see Monitor.h) to own the boot/init sequence: status
//   LED, sensor init hook, WiFi, daily rebooter, OTA, and the standard InfluxDB
//   setup/post/flush cycle.
// - This device's bucket/site/location is prompted for over Serial the first time it
//   runs, then saved to Preferences (NVS) so it survives reboots and OTA firmware
//   updates. On subsequent boots the saved value is used automatically, unless a Serial
//   monitor is attached at boot, which offers a re-prompt. This is handled by the
//   shared Monitor class (see Monitor.h) via SKETCH_CONFIG's useBucketPrompt flag,
//   since this sketch prompts for a bucket/site (chosen from a fixed list) and a
//   free-text location, rather than picking a single fixed SiteConfig entry.
// - Samples temperature and humidity every SENSOR_INTERVAL_MS and accumulates
//   time-averaged values for the next upload.
// - Verifies Wi-Fi connectivity each loop (handled by SketchBase::loop()) and resets
//   the device after config.wifiLostResetDelayS seconds if it cannot reconnect.
// - Posts telemetry to InfluxDB every INFLUX_INTERVAL_S seconds (handled by Monitor::loop()).
// - Checks for a firmware update periodically and, if a newer version is published,
//   downloads and installs it before restarting.
//
// Failure handling:
// - Sensor initialization failure triggers a device reset after config.sensorFailureResetDelayS seconds.
// - Influx initialization failure (handled by Monitor::begin()) triggers a device reset.
// - Runtime InfluxDB post/flush failures are logged to Serial by Monitor::loop() and
//   retried the following cycle.
//
// Outputs:
// - Serial: sensor type, address, and ID printed during initialization; the resolved (or
//   prompted-for) site/location printed after WiFi connects.
//
// Usage:
// - Flash to a Waveshare ESP32-S3-Zero (wired per WaveShare_ESP32_S3_Zero_Sensors).
// - Power on and allow initialization to complete.
// - Observe periodic telemetry uploads in InfluxDB.
//
// InfluxDB points uploaded (Measurement: Sensors):
//
// - site=<from Serial prompt/Preferences>, location=<from Serial prompt/Preferences>, sensor=Temperature
//     temperature: time-averaged value of sensor.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, averaged over the INFLUX_INTERVAL_S upload interval.
//     humidity: time-averaged value of sensor.readHumidity(), sampled every
//     SENSOR_INTERVAL_MS, averaged over the INFLUX_INTERVAL_S upload interval.
//     dewPoint, absoluteHumidity, heatIndex: time-averaged values derived from the
//     same temperature/humidity reading (see TempSensor::readAll()), sampled every
//     SENSOR_INTERVAL_MS, averaged over the INFLUX_INTERVAL_S upload interval.
//

// This board is wired with an onboard NeoPixel status LED and no display or buttons.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"

#ifdef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch is for boards without a display (e.g. Waveshare ESP32-S3-Zero); use Temp_Monitor_Display instead."
#endif
#ifndef ARDUINO_NEOPIXEL_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Waveshare ESP32-S3-Zero)."
#endif

#include "TempSensor.h"
#include "SerialX.h"
#include "Timer.h"

#include "WiFiSettings.h"

#include "Monitor.h"

// version.txt contains a quoted version string (e.g. "v1.1") and is included directly here
// so the compiled-in VERSION always matches the same file uploaded to the GitHub release,
// with no separate sync step required.
constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Temp_Monitor";
constexpr auto INFLUX_SENSOR = "Temperature";
constexpr auto PREFERENCES_NAMESPACE = "TempMonitor";
constexpr uint8_t INFLUX_INTERVAL_S = 15;
constexpr uint16_t SENSOR_INTERVAL_MS = 500;
constexpr uint8_t INFLUX_TEMP_DECIMAL_PLACES = 3;
constexpr uint8_t INFLUX_HUMIDITY_DECIMAL_PLACES = 2;

Arduino arduino;
TempSensor sensor;
Timer sensorTimer(SENSOR_INTERVAL_MS);

InfluxField* tempField = nullptr;
InfluxField* humField = nullptr;
InfluxField* dewPointField = nullptr;
InfluxField* absoluteHumidityField = nullptr;
InfluxField* heatIndexField = nullptr;

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .influxSensor = INFLUX_SENSOR,
   .influxIntervalS = INFLUX_INTERVAL_S,
   .enableOTA = true,
   .enableRebooter = true,
   .useBucketPrompt = true,
};

Monitor monitor(&arduino, SKETCH_CONFIG);

void setup()
{
   Wire.begin();

   monitor.begin();

   // Fall back to the internal ESP32 CPU temperature sensor if no external sensor is
   // found, so the device still reports a (less accurate) temperature reading instead
   // of failing to start.
   if (arduino.initSensor("Sensor", []() { return sensor.begin(false, true); }, []() { return sensor.type(); }))
   {
      std::string addressStr = sensor.address() != 0 ? std::string("0x") + String(sensor.address(), HEX).c_str() : "N/A";
      std::string idStr = strlen(sensor.id()) > 0 ? sensor.id() : "N/A";
      std::string sensorMessage = std::string("Sensor: ") + sensor.type() + ", Address: " + addressStr + ", ID: " + idStr;
      monitor.logMessage(sensorMessage.c_str());
   }
   else
   {
      monitor.reportSensorFailure();
   }

   InfluxPoint* point = monitor.addPoint(INFLUX_SENSOR);
   tempField = point->addTimeAverageField(INFLUX_INTERVAL_S, "temperature", INFLUX_TEMP_DECIMAL_PLACES);
   humField = point->addTimeAverageField(INFLUX_INTERVAL_S, "humidity", INFLUX_HUMIDITY_DECIMAL_PLACES);
   dewPointField = point->addTimeAverageField(INFLUX_INTERVAL_S, "dewPoint", INFLUX_TEMP_DECIMAL_PLACES);
   absoluteHumidityField = point->addTimeAverageField(INFLUX_INTERVAL_S, "absoluteHumidity", INFLUX_HUMIDITY_DECIMAL_PLACES);
   heatIndexField = point->addTimeAverageField(INFLUX_INTERVAL_S, "heatIndex", INFLUX_TEMP_DECIMAL_PLACES);
}

void loop()
{
   monitor.loop();

   if (sensorTimer.ready())
   {
      Readings readings = sensor.readAll();
      tempField->set(readings.tempF);
      humField->set(readings.humidity);
      dewPointField->set(readings.dewPointF);
      absoluteHumidityField->set(readings.absoluteHumidity);
      heatIndexField->set(readings.heatIndexF);
   }
}
