//
// Lake water temperature monitoring station with multi-sensor support.
//
// Monitors temperature and humidity at 5 different locations in a lake using I2C multiplexing.
// Uses the shared MonitorSketch class (see MonitorSketch.h) to own the boot/init sequence: status LED,
// per-sensor init, WiFi, daily rebooter, OTA, task watchdog, and the standard InfluxDB
// setup/post/flush cycle.
//
// Uploads to InfluxDB as measurement "Sensors", tagged with site="Lake", location="Dock",
// and item=<Surface|Bottom 1|Bottom 2|Enclosure> identifying which sensor the point came
// from. Fields are "temperature" and "humidity", each averaged over SENSOR_AVERAGE_PERIOD_S
// seconds, plus a point-in-time CPU temperature reading via InfluxConfig.includeCpuTemp.
//
// Checks for a firmware update periodically.
//

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include <array>

#include "ArduinoBoard.h"
#include "I2CMultiplexor.h"
#include "LibraryVersion.h"
#include "SerialTable.h"
#include "TempSensor.h"

#include "WiFiSettings.h"

#include "MonitorSketch.h"

// This sketch's own version (e.g. "1.1"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("1.1");
constexpr auto SKETCH_NAME = "Lake_Temp_Monitor";

// Influx database settings
constexpr auto INFLUX_SITE = "Lake";
constexpr auto INFLUX_LOCATION = "Dock";

// ----------- InfluxDB bucket selection
constexpr InfluxContext INFLUX_PROMPTS[] = {
   { INFLUXDB_BUCKET, INFLUX_SITE, INFLUX_LOCATION },
};

///
/// <summary>
/// Static configuration for a single sensor location: its multiplexor port and its
/// item/tag name.
/// </summary>
///
struct SensorConfig
{
   uint8_t port;
   const char* item;
};

// Sensor configuration
constexpr std::array SENSOR_CONFIGS = {
   SensorConfig{ 3, "Surface" },
   SensorConfig{ 0, "Bottom 1" },
   SensorConfig{ 1, "Bottom 2" },
   SensorConfig{ 2, "Enclosure" },
};
constexpr uint8_t NUM_SENSORS = SENSOR_CONFIGS.size();
// ----------- Timing (managed by this sketch, not by InfluxConfig)
constexpr uint16_t SENSOR_INTERVAL_MS = 200;  // read the sensors every N ms
constexpr uint16_t UPLOAD_INTERVAL_S = 15;    // upload to InfluxDB every N seconds
constexpr float SENSOR_AVERAGE_PERIOD_S = UPLOAD_INTERVAL_S;  // each upload is the average over the whole upload interval

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring.
I2CMultiplexor multi;

// Sensor arrays
std::array<TempSensor*, NUM_SENSORS> sensors;
std::array<InfluxField*, NUM_SENSORS> tempFields;
std::array<InfluxField*, NUM_SENSORS> humFields;

Timer sensorTimer(SENSOR_INTERVAL_MS);
TimerSecs uploadTimer(UPLOAD_INTERVAL_S);

InfluxConfig INFLUX_CONFIG = {
   .prompts = INFLUX_PROMPTS,
};

// Preferences (NVS) namespace names are limited to 15 characters; SKETCH_NAME
// ("Lake_Temp_Monitor") exceeds that, so a shorter dedicated namespace is used instead.
constexpr auto PREFERENCES_NAMESPACE = "LakeTempMonitor";

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

MonitorSketch sketch(SKETCH_CONFIG, INFLUX_CONFIG);

///
/// <summary>
/// Prints a summary table of all detected sensors: I2C mux port, I2C address, sensor
/// type, and location/item tag.
/// </summary>
///
void printSensorSummary()
{
   static const SerialTable::Column columns[] = {
      { "Connection", 18 },
      { "Address", 10 },
      { "Type", 8 },
      { "Tag", 18 },
   };
   SerialTable table("Detected Sensors", columns);
   String output = table.formatHeader();

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (!sensors[i]->exists())
      {
         continue;
      }

      String connection = String("I2C Mux Port ") + SENSOR_CONFIGS[i].port;
      String address = String("0x") + String(sensors[i]->address(), HEX);

      String tag = String(INFLUX_SITE) + "/" + INFLUX_LOCATION + "/" + SENSOR_CONFIGS[i].item;

      output += table.formatRow(connection, address, sensors[i]->type(), tag);
   }

   Logger.log(output);
}

void setup()
{
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensors[i] = new TempSensor();
   }

   sketch.addSensor("Surface", []() { multi.select(SENSOR_CONFIGS[0].port); return sensors[0]->begin(true); }, nullptr, false);
   sketch.addSensor("Bottom 1", []() { multi.select(SENSOR_CONFIGS[1].port); return sensors[1]->begin(true); }, nullptr, false);
   sketch.addSensor("Bottom 2", []() { multi.select(SENSOR_CONFIGS[2].port); return sensors[2]->begin(true); }, nullptr, false);
   sketch.addSensor("Enclosure", []() { multi.select(SENSOR_CONFIGS[3].port); return sensors[3]->begin(true); }, nullptr, false);

   sketch.begin();

   printSensorSummary();

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      InfluxPoint* point = sketch.addPoint({ { "item", SENSOR_CONFIGS[i].item } });
      tempFields[i] = point->addTimeAverageField(SENSOR_AVERAGE_PERIOD_S, "temperature", 3);
      humFields[i] = point->addTimeAverageField(SENSOR_AVERAGE_PERIOD_S, "humidity", 2);
   }

   Logger.logInitializationComplete();
}

void loop()
{
   sketch.loop();

   if (sensorTimer.ready())
   {
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (sensors[i]->exists())
         {
            multi.select(SENSOR_CONFIGS[i].port);
            tempFields[i]->set(sensors[i]->readTemperatureF());
            humFields[i]->set(sensors[i]->readHumidity());
         }
      }
   }

   if (uploadTimer.ready())
   {
      sketch.postPoints();
   }
}
