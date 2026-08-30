//
// Wind Publisher
//
// Reads wind speed from an anemometer and publishes live readings over a WebSocket
// telemetry connection, while also uploading rolling-averaged enclosure and CPU
// temperature/humidity readings to InfluxDB on a fixed interval.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   streams live wind speed readings as they're read.
// - Samples enclosure temperature/humidity and CPU temperature every SENSOR_INTERVAL_MS
//   and accumulates rolling averages for the next InfluxDB upload.
// - Prints wind speed and temperature readings to Serial every SERIAL_INTERVAL_MS.
// - Posts telemetry to InfluxDB every INFLUX_INTERVAL_S seconds.
// - Resets the device on telemetry disconnect or error.
// - Restarts the device every 24 hours to play it safe.
//
// InfluxDB points uploaded (Measurement: Sensors):
//
// - site=Lake, location=Dock, sensor=Wind, item=Enclosure
//     temperature: rolling average of enclosureTemp.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//     humidity: rolling average of enclosureTemp.readHumidity(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//
// - site=Lake, location=Dock, sensor=Wind, item=CPU
//     temperature: rolling average of cpuTemp.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

constexpr auto TELEMETRY_TOPIC = "Wind/Lake";

// ----------- InfluxDB settings
constexpr auto INFLUX_MEASUREMENT = "Sensors";
constexpr auto INFLUX_SITE = "Lake";
constexpr auto INFLUX_LOCATION = "Dock";
constexpr auto INFLUX_SENSOR = "Wind";
constexpr uint16_t INFLUX_INTERVAL_S = 60;
constexpr uint8_t INFLUX_DECIMALS = 2;
constexpr size_t INFLUX_ROLLING_SAMPLES = 10;
constexpr uint8_t INFLUX_BATCH_SIZE = 2; // enclosure + CPU points

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "ESP32TempSensor.h"
#include "Influx.h"
#include "SerialX.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "TempSensor.h"
#include "Timer.h"
#include "WindMeter.h"

#include "WiFiSettings.h"

// ----------- Telemetry
constexpr uint8_t NUM_DECIMALS = 2;
constexpr uint16_t SERIAL_INTERVAL_MS = 5000;
constexpr uint16_t SENSOR_INTERVAL_MS = 100;
Timer serialTimer(SERIAL_INTERVAL_MS);
Timer sensorTimer(SENSOR_INTERVAL_MS);

// ----------- Wind sensor pins
constexpr uint8_t WIND_SENSOR_PIN = 11;
constexpr uint8_t WIND_SENSOR_GROUND_PIN = 13; // held LOW to power the wind encoder/sensor
constexpr uint8_t WIND_SENSOR_POWER_PIN = 12; // held HIGH to power the wind encoder/sensor

// ----------- CPU throttling
constexpr uint8_t CPU_FREQUENCY_MHZ = 80; // keep things cool

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring.
Arduino arduino;
WindMeter wind(WIND_SENSOR_PIN, arduino.ledPin(), LEDColor::CLEAR_PINK);
Influx influx(INFLUX_INTERVAL_S, &arduino);

TempSensor enclosureTemp;
ESP32TempSensor cpuTemp;

TelemetryEventHandler telemetryHandler(&arduino);
TelemetryPublisher client(TELEMETRY_TOPIC, NUM_DECIMALS, &arduino, &telemetryHandler);

InfluxPoint enclosurePoint(INFLUX_MEASUREMENT, { { "site", INFLUX_SITE }, { "location", INFLUX_LOCATION }, { "sensor", INFLUX_SENSOR }, { "item", "Enclosure" } });
InfluxPoint cpuPoint(INFLUX_MEASUREMENT, { { "site", INFLUX_SITE }, { "location", INFLUX_LOCATION }, { "sensor", INFLUX_SENSOR }, { "item", "CPU" } });
InfluxField* enclosureTempField = enclosurePoint.addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);
InfluxField* enclosureHumidityField = enclosurePoint.addRollingAverageField(INFLUX_ROLLING_SAMPLES, "humidity", INFLUX_DECIMALS);
InfluxField* cpuTempField = cpuPoint.addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);

void setup()
{
   SerialX::begin();
   Serial.println("Wind Publisher");

   // power the wind encoder/sensor
   pinMode(WIND_SENSOR_GROUND_PIN, OUTPUT);
   pinMode(WIND_SENSOR_POWER_PIN, OUTPUT);
   digitalWrite(WIND_SENSOR_GROUND_PIN, LOW);
   digitalWrite(WIND_SENSOR_POWER_PIN, HIGH);

   arduino.begin(); // sets up the I2C bus/power rail and the RGB status LED

   enclosureTemp.begin();
   cpuTemp.begin();

   wind.begin();

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &arduino);
   if (!influx.begin(arduino))
   {
      arduino.setStatus(Status::FAILED);
      delay(1000); // time for LED to show
      Util::reset();
   }

   influx.client()->setWriteOptions(WriteOptions().batchSize(INFLUX_BATCH_SIZE).bufferSize(2 * INFLUX_BATCH_SIZE));

   arduino.enableRebooter();

   arduino.initClient("WebSocket", []() { client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &arduino);

   setCpuFrequencyMhz(CPU_FREQUENCY_MHZ);
}

void loop()
{
   if (client.isStarted())
   {
      // without a delay, the waveshare crashes
      delay(1);

      float speed = wind.getSpeed();
      client.setValue(speed);
   }

   client.loop(); // Continuously poll for events and maintain connection

   if (sensorTimer.ready())
   {
      enclosureTempField->set(enclosureTemp.readTemperatureF());
      enclosureHumidityField->set(enclosureTemp.readHumidity());
      cpuTempField->set(cpuTemp.readTemperatureF());
   }

   if (serialTimer.ready())
   {
      Serial.print("Wind speed: ");
      Serial.print(wind.getSpeed());
      Serial.println(" m/s");

      Serial.print("Enclosure temp: ");
      Serial.print(enclosureTemp.readTemperatureF());
      Serial.println(" °F");

      Serial.print("Enclosure humidity: ");
      Serial.print(enclosureTemp.readHumidity());
      Serial.println(" %");

      Serial.print("CPU temp: ");
      Serial.print(cpuTemp.readTemperatureF());
      Serial.println(" °F");
   }

   if (client.isStarted() && influx.ready())
   {
      enclosurePoint.post(influx.client(), true);
      cpuPoint.post(influx.client(), true);

      // Both points above were only queued into the write buffer (see INFLUX_BATCH_SIZE),
      // so flush now to post them together in a single HTTP request sharing one timestamp.
      if (!influx.client()->flushBuffer())
      {
         Serial.print("InfluxDB flush failed: ");
         Serial.println(influx.client()->getLastErrorMessage());
      }
   }
}

