//
// Lake water temperature monitoring station with multi-sensor support.
//
// Monitors temperature and humidity at 5 different locations in a lake using I2C multiplexing.
// Uses the shared Monitor class (see Monitor.h) to own the boot/init sequence: status LED,
// per-sensor init, WiFi, daily rebooter, OTA, task watchdog, and the standard InfluxDB
// setup/post/flush cycle.
//
// Uploads to InfluxDB as measurement "Sensors", tagged with site="Lake", location="Dock",
// sensor="Temperature", and item=<Surface|Bottom 1|Bottom 2|Enclosure|CPU> identifying which
// sensor the point came from. Fields are "temperature" and "humidity", each averaged over
// SENSOR_AVERAGE_PERIOD_S before being posted.
//
// Checks for a firmware update periodically.
//

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include <array>

#include "ArduinoBoard.h"
#include "ESP32TempSensor.h"
#include "I2CMultiplexor.h"
#include "LibraryVersion.h"
#include "SerialTable.h"
#include "TempSensor.h"

#include "WiFiSettings.h"

#include "Monitor.h"

// This sketch's own version (e.g. "1.1"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("1.1");
constexpr auto SKETCH_NAME = "Lake_Temp_Monitor";

// Influx database settings
constexpr auto INFLUX_SITE = "Lake";
constexpr auto INFLUX_LOCATION = "Dock";
constexpr auto INFLUX_SENSOR = "Temperature";
constexpr auto INFLUX_INTERVAL_S = 15;  // Log data to InfluxDB every N seconds

// ----------- InfluxDB bucket selection (production vs testing)
constexpr InfluxContext INFLUX_PROMPTS[] = {
   { INFLUXDB_BUCKET, INFLUX_SITE, INFLUX_LOCATION, INFLUX_SENSOR },
   { "Testing", INFLUX_SITE, INFLUX_LOCATION, INFLUX_SENSOR },
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
   SensorConfig{ 4, "CPU" },  // CPU uses the built-in ESP32 sensor; port is unused
};
constexpr uint8_t NUM_SENSORS = SENSOR_CONFIGS.size();
constexpr uint8_t CPU_SENSOR_INDEX = NUM_SENSORS - 1;  // Last sensor is the built-in ESP32 CPU sensor
constexpr uint16_t SENSOR_INTERVAL_MS = 200;
constexpr float SENSOR_AVERAGE_PERIOD_S = 2.0f;  // 2 secs, equivalent to 10 samples at SENSOR_INTERVAL_MS

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring.
Arduino arduino;
I2CMultiplexor multi;

// Sensor arrays
std::array<TempSensor*, NUM_SENSORS> sensors;
std::array<InfluxField*, NUM_SENSORS> tempFields;
std::array<InfluxField*, NUM_SENSORS> humFields;

Timer sensorTimer(SENSOR_INTERVAL_MS);

InfluxConfig INFLUX_CONFIG = {
   .prompts = INFLUX_PROMPTS,
   .intervalS = INFLUX_INTERVAL_S,
};

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = SKETCH_NAME,
   .influx = INFLUX_CONFIG,
   .enableOTA = true,
   .enableRebooter = true,
};

Monitor monitor(&arduino, SKETCH_CONFIG);

///
/// <summary>
/// Adds each sensor's current temperature/humidity readings to a GetStatus reply,
/// on top of Logger's/SketchBase's base fields. Reads each sensor live rather than
/// reporting a cached value, since GetStatus is infrequent and can afford the read.
/// </summary>
/// <param name="status">The in-progress status to add fields to.</param>
///
void onStatus(LoggerStatus& status)
{
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (sensors[i]->exists())
      {
         multi.select(SENSOR_CONFIGS[i].port);
         status.add(std::string(SENSOR_CONFIGS[i].item) + " Temperature", sensors[i]->readTemperatureF(), 3);
         status.add(std::string(SENSOR_CONFIGS[i].item) + " Humidity", sensors[i]->readHumidity(), 2);
      }
   }
}

///
/// <summary>
/// Prints a summary table of all detected sensors:
/// port, direct I2C, or the built-in ESP32 sensor), its I2C address, sensor type, and
/// its location/item tag.
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
   table.printHeader();

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (!sensors[i]->exists())
      {
         continue;
      }

      String connection;
      String address;
      if (i == CPU_SENSOR_INDEX)
      {
         connection = "ESP32 Direct";
         address = "";
      }
      else
      {
         connection = String("I2C Mux Port ") + SENSOR_CONFIGS[i].port;
         address = String("0x") + String(sensors[i]->address(), HEX);
      }

      String tag = String(INFLUX_SITE) + "/" + INFLUX_LOCATION + "/" + SENSOR_CONFIGS[i].item;

      table.printRow(connection, address, sensors[i]->type(), tag);
   }
}

void setup()
{
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensors[i] = new TempSensor();
   }

   monitor.addSensor("Surface", []() { multi.select(SENSOR_CONFIGS[0].port); return sensors[0]->begin(true); }, nullptr, false);
   monitor.addSensor("Bottom 1", []() { multi.select(SENSOR_CONFIGS[1].port); return sensors[1]->begin(true); }, nullptr, false);
   monitor.addSensor("Bottom 2", []() { multi.select(SENSOR_CONFIGS[2].port); return sensors[2]->begin(true); }, nullptr, false);
   monitor.addSensor("Enclosure", []() { multi.select(SENSOR_CONFIGS[3].port); return sensors[3]->begin(true); }, nullptr, false);
   monitor.addSensor("CPU", []() { return sensors[CPU_SENSOR_INDEX]->begin(new ESP32TempSensor(), true); }, nullptr, false);

   monitor.begin();
   monitor.onStatus(onStatus);

   printSensorSummary();

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      InfluxPoint* point = monitor.addPoint({ { "sensor", INFLUX_SENSOR }, { "item", SENSOR_CONFIGS[i].item } });
      tempFields[i] = point->addTimeAverageField(SENSOR_AVERAGE_PERIOD_S, "temperature", 3);
      humFields[i] = point->addTimeAverageField(SENSOR_AVERAGE_PERIOD_S, "humidity", 2);
   }

   Logger.logInitializationComplete();
}

void loop()
{
   monitor.loop();

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
}
