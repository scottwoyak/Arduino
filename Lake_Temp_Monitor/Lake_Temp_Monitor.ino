//
// Lake water temperature monitoring station with multi-sensor support.
//
// Monitors temperature and humidity at 5 different locations in a lake using I2C multiplexing.
// Logs readings to InfluxDB at configurable intervals. Includes watchdog for automatic reset
// on communication failures and daily reboot to manage long-term stability.
//

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include <Arduino.h>
#include <Adafruit_SleepyDog.h>
#include <array>
#include <time.h>

#include "ArduinoBoard.h"
#include "ESP32TempSensor.h"
#include "I2CMultiplexor.h"
#include "Influx.h"
#include "Rebooter.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "Status.h"
#include "TempSensor.h"
#include "Timer.h"

#include "WiFiSettings.h"

// Influx database settings
constexpr auto INFLUX_MEASUREMENT = "Sensors";
constexpr auto INFLUX_LOCATION = "Lake";
constexpr auto INFLUX_INTERVAL_S = 15;       // Log data to InfluxDB every N seconds
constexpr auto WATCHDOG_INTERVAL_S = 60;     // Reboot if no successful log in N seconds
constexpr auto WATCHDOG_STARTUP_M = 5;        // Reboot if startup fails in N minutes
constexpr auto WIFI_RESET_DELAY_S = 10;

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
constexpr auto INFLUX_BATCH_SIZE = NUM_SENSORS;  // Batch all sensor points into a single HTTP write
constexpr uint16_t SENSOR_INTERVAL_MS = 200;
constexpr float SENSOR_AVERAGE_PERIOD_S = 2.0f;  // 2 secs, equivalent to 10 samples at SENSOR_INTERVAL_MS

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring.
Arduino arduino;
I2CMultiplexor multi;

// Sensor arrays
std::array<TempSensor*, NUM_SENSORS> sensors;
std::array<InfluxPoint*, NUM_SENSORS> points;
std::array<InfluxField*, NUM_SENSORS> tempFields;
std::array<InfluxField*, NUM_SENSORS> humFields;

Influx influx(INFLUX_INTERVAL_S, &arduino);
Timer sensorTimer(SENSOR_INTERVAL_MS);
Rebooter rebooter;

///
/// <summary>
/// Prints a summary table of all detected sensors: how each is connected (multiplexor
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

      String tag = String(INFLUX_LOCATION) + "/" + SENSOR_CONFIGS[i].item;

      table.printRow(connection, address, sensors[i]->type(), tag);
   }
}

void setup()
{
   // Enable watchdog for startup supervision (5 minutes)
   Watchdog.enable(WATCHDOG_STARTUP_M * 60 * 1000);

   // Create sensor objects and data structures
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensors[i] = new TempSensor();
      points[i] = new InfluxPoint(INFLUX_MEASUREMENT);
      tempFields[i] = points[i]->addTimeAverageField(SENSOR_AVERAGE_PERIOD_S, "temperature", 3);
      humFields[i] = points[i]->addTimeAverageField(SENSOR_AVERAGE_PERIOD_S, "humidity", 2);
   }

   SerialX::begin();

   arduino.begin(); // sets up the I2C bus/power rail, the RGB status LED, and the activity LED
   arduino.beginInit("Initializing Lake Temperature Monitor");
   arduino.setStatus(Status::STARTED);

   // Initialize and detect all sensors
   Serial.println("Detecting sensors...");
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      Serial.print("  Sensor ");
      Serial.print(i);
      Serial.print(" (");
      Serial.print(SENSOR_CONFIGS[i].item);
      Serial.print(")... ");

      bool sensorFound;
      if (i == CPU_SENSOR_INDEX)
      {
         // Built-in ESP32 CPU temperature sensor
         sensorFound = sensors[i]->begin(new ESP32TempSensor(), true);
      }
      else
      {
         multi.select(SENSOR_CONFIGS[i].port);
         sensorFound = sensors[i]->begin(true);
      }

      if (sensorFound)
      {
         Serial.print("OK - ");
         Serial.print(sensors[i]->type());
         Serial.print(" (0x");
         Serial.print(sensors[i]->address(), HEX);
         Serial.println(")");
      }
      else
      {
         Serial.println("NOT FOUND");
      }
   }

   printSensorSummary();

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &arduino);

   // Initialize InfluxDB connection
   if (!influx.begin(arduino))
   {
      arduino.setStatus(Status::FAILED);
      Util::reset(WIFI_RESET_DELAY_S);
   }

   // Batch all sensor points into a single HTTP write so a slow blocking network write for
   // one sensor doesn't let later sensors' time-averaged fields expire before they're posted.
   influx.client()->setWriteOptions(WriteOptions().batchSize(INFLUX_BATCH_SIZE).bufferSize(2 * INFLUX_BATCH_SIZE));

   // Record the current day so loop() can reboot once the date advances
   rebooter.begin();

   // Tag each data point with the lake location and its specific item
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      points[i]->addTag("location", INFLUX_LOCATION);
      points[i]->addTag("item", SENSOR_CONFIGS[i].item);
   }

   // Reduce CPU frequency for lower power consumption
   setCpuFrequencyMhz(80);

   arduino.setStatus(Status::READY);

   // Enable watchdog for operation (60 seconds between successful logs)
   Watchdog.enable(WATCHDOG_INTERVAL_S * 1000);
}

void loop()
{
   // Perform a daily reboot for long-term stability, as soon as the date advances past
   // the day the sketch started. The system clock is synced via NTP (see influx.begin()
   // in setup()), so this checks wall-clock time rather than elapsed millis().
   rebooter.loop();

   arduino.led.turnOff();  // Turn off activity LED (turned on during data upload)

   // Read temperature and humidity from all available sensors
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

   // Ensure WiFi connectivity
   if (!arduino.ensureWiFiConnected(&arduino))
   {
      Serial.println("WiFi reconnection failed, performing reset");
      Util::reset(WIFI_RESET_DELAY_S);
   }

   // Upload data points to InfluxDB at configured interval. The InfluxDBClient's batch size is
   // set to NUM_SENSORS (the maximum possible), so points are only queued locally here rather
   // than immediately triggering a blocking network write - avoiding a slow per-sensor write
   // from letting later sensors' time-averaged fields expire while earlier sensors were still
   // being posted. Since a missing/failed sensor means the batch may never actually fill up,
   // flushBuffer() is called afterward to force the queued points out regardless of count.
   if (influx.ready())
   {
      arduino.led.turnOn();  // Indicate data transmission activity

      bool anyQueued = false;
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (!sensors[i]->exists())
         {
            continue;  // Skip sensors that weren't detected
         }

         if (points[i]->post(influx.client(), true))
         {
            anyQueued = true;
         }
         else
         {
            Serial.print("InfluxDB queue failed for sensor ");
            Serial.print(i);
            Serial.print(": ");
            Serial.println(influx.client()->getLastErrorMessage());
         }
      }

      if (anyQueued)
      {
         if (influx.client()->flushBuffer())
         {
            // Only reset watchdog on successful write
            Watchdog.reset();
         }
         else
         {
            Serial.print("InfluxDB flush failed: ");
            Serial.println(influx.client()->getLastErrorMessage());
         }
      }
   }
}


