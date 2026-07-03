//
// Lake water temperature monitoring station with multi-sensor support.
//
// Monitors temperature and humidity at 5 different locations in a lake using I2C multiplexing.
// Logs readings to InfluxDB at configurable intervals. Includes watchdog for automatic reset
// on communication failures and daily reboot to manage long-term stability.
//

#include <Arduino.h>
#include <Adafruit_SleepyDog.h>

#include "ESP32TempSensor.h"
#include "I2CMultiplexor.h"
#include "Influx.h"
#include "SerialX.h"
#include "Status.h"
#include "TempSensor.h"
#include "Timer.h"

#include "WiFiSettings.h"

// Influx database settings
constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15;       // Log data to InfluxDB every N seconds
constexpr auto WATCHDOG_INTERVAL_S = 60;     // Reboot if no successful log in N seconds
constexpr auto WATCHDOG_STARTUP_MS = 5 * 60 * 1000;  // Reboot if startup fails in N ms
constexpr auto REBOOT_INTERVAL_MS = 24 * 60 * 60 * 1000;  // Daily reboot for stability
constexpr auto WIFI_RESET_DELAY_S = 10;

// Sensor configuration
constexpr uint8_t NUM_SENSORS = 5;
constexpr uint8_t SENSOR_PORTS[] = {0, 1, 2, 3, 4};  // Last port directly talks to ESP32

// I2C pins (custom configuration)
constexpr auto I2C_SCL = 8;
constexpr auto I2C_SDA = 7;

// LED pins
constexpr auto WHITE_LED_PIN = 5;
constexpr auto BLUE_LED_PIN = 2;
constexpr auto GREEN_LED_PIN = 3;
constexpr auto RED_LED_PIN = 4;

// Sensor location names
const char* SENSOR_LOCATIONS[] = {
   "New Surface",
   "New 3 Feet",
   "New Bottom",
   "New Enclosure",
   "New CPU",
};

I2CMultiplexor multi;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Timer influxTimer(INFLUX_INTERVAL_S * 1000);

// Sensor arrays
TempSensor* sensors[NUM_SENSORS];
InfluxPoint* points[NUM_SENSORS];
InfluxField* tempFields[NUM_SENSORS];
InfluxField* humFields[NUM_SENSORS];

// Status indicators
LedStatus status(WHITE_LED_PIN, BLUE_LED_PIN, GREEN_LED_PIN);
LED redLed(RED_LED_PIN);
Influx influx(WIFI_SSID, WIFI_PASSWORD, &client, &status);

void setup()
{
   // Enable watchdog for startup supervision (5 minutes)
   Watchdog.enable(WATCHDOG_STARTUP_MS);

   // Create sensor objects and data structures
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensors[i] = new TempSensor();
      points[i] = new InfluxPoint(INFLUX_MEASUREMENT);
      tempFields[i] = points[i]->addTimeAveragedField(INFLUX_INTERVAL_S, "temperature", 3);
      humFields[i] = points[i]->addTimeAveragedField(INFLUX_INTERVAL_S, "humidity", 2);
   }

   SerialX::begin();
   Serial.println("Initializing Lake Temperature Monitor");

   // Initialize I2C with custom pins
   Wire.begin(I2C_SDA, I2C_SCL);

   status.begin();
   redLed.begin();

   // Initialize and detect all sensors
   Serial.println("Detecting sensors...");
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      Serial.print("  Sensor ");
      Serial.print(i);
      Serial.print(" (");
      Serial.print(SENSOR_LOCATIONS[i]);
      Serial.print(")... ");

      bool sensorFound;
      if (i == NUM_SENSORS - 1)
      {
         // Last sensor is built-in ESP32 CPU temperature
         sensorFound = sensors[i]->begin(new ESP32TempSensor(), true);
      }
      else
      {
         // Other sensors on I2C multiplexor
         multi.select(SENSOR_PORTS[i]);
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

   // Initialize InfluxDB connection
   if (!influx.begin())
   {
      Util::reset(WIFI_RESET_DELAY_S);
   }

   // Tag each data point with its location
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      points[i]->addTag("location", SENSOR_LOCATIONS[i]);
   }

   // Reduce CPU frequency for lower power consumption
   setCpuFrequencyMhz(80);

   // Enable watchdog for operation (60 seconds between successful logs)
   Watchdog.enable(WATCHDOG_INTERVAL_S * 1000);
}

void loop()
{
   // Perform daily reboot for long-term stability
   if (millis() > REBOOT_INTERVAL_MS)
   {
      Serial.println("Performing scheduled 24-hour reboot");
      Util::reset();
   }

   redLed.turnOff();  // Turn off activity LED (turned on during data upload)

   // Read temperature and humidity from all available sensors
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (sensors[i]->exists())
      {
         multi.select(SENSOR_PORTS[i]);
         tempFields[i]->set(sensors[i]->readTemperatureF());
         humFields[i]->set(sensors[i]->readHumidity());
      }
   }

   // Ensure WiFi connectivity
   if (!influx.ensureWiFiConnected())
   {
      Serial.println("WiFi reconnection failed, performing reset");
      Util::reset(WIFI_RESET_DELAY_S);
   }

   // Upload data points to InfluxDB at configured interval
   if (influxTimer.ready())
   {
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (!sensors[i]->exists())
         {
            continue;  // Skip sensors that weren't detected
         }

         redLed.turnOn();  // Indicate data transmission activity

         if (!points[i]->post(&client, true))
         {
            Serial.print("InfluxDB write failed for sensor ");
            Serial.print(i);
            Serial.print(": ");
            Serial.println(client.getLastErrorMessage());
         }
         else
         {
            // Only reset watchdog on successful write
            Watchdog.reset();
         }
      }
   }
}


