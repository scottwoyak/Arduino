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
// - Prints distance and wave height (relative to this device's own running average,
//   for local LED/Serial feedback only) readings to Serial every publish cycle.
// - Drives the general-purpose LED at full brightness while starting up, then switches
//   to a brightness proportional to wave height once telemetry is connected: off at
//   or below LED_WAVE_HEIGHT_LOW_CM, full at or above LED_WAVE_HEIGHT_HIGH_CM, and
//   linearly interpolated in between.
// - Posts the average depth, enclosure temperature/humidity, and CPU temperature to
//   InfluxDB every INFLUX_INTERVAL_S seconds, printing the same values to Serial at
//   that time.
// - Restarts the device every 24 hours to play it safe, and on telemetry disconnect
//   or error.
//
// InfluxDB points uploaded (Measurement: Sensors):
//
// - site=Lake, location=Dock, sensor=WaveHeight
//     avgDepth: rolling average of raw depth readings (depth->getDepth()) over the
//     5 minute averaging window (DepthSensorBase::DEFAULT_AVERAGE_DURATION_M); only
//     posted once that window is fully populated (depth->isAverageFull()).
//
// - site=Lake, location=Dock, sensor=WaveHeight, item=Enclosure
//     temperature: rolling average of enclosureTemp.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//     humidity: rolling average of enclosureTemp.readHumidity(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//
// - site=Lake, location=Dock, sensor=WaveHeight, item=CPU
//     temperature: rolling average of cpuTemp.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

constexpr auto TELEMETRY_TOPIC = "Waves/LakeP";

// ----------- InfluxDB settings
constexpr auto INFLUX_MEASUREMENT = "Sensors";
constexpr auto INFLUX_SITE = "Lake";
constexpr auto INFLUX_LOCATION = "Dock";
constexpr auto INFLUX_SENSOR = "WaveHeight";
constexpr uint16_t INFLUX_INTERVAL_S = 60;
constexpr uint8_t INFLUX_DECIMALS = 1;
constexpr uint8_t INFLUX_AVG_DEPTH_DECIMALS = 2;
constexpr size_t INFLUX_ROLLING_SAMPLES = 10;
constexpr uint8_t INFLUX_BATCH_SIZE = 3; // depth + enclosure + CPU points

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "DepthSensorBase.h"
#include "ESP32TempSensor.h"
#include "Influx.h"
#include "SerialX.h"
#include "SHT3xTempSensor.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "WiFiSettings.h"

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

// ----------- Telemetry
constexpr uint8_t NUM_DECIMALS = 1;
constexpr uint16_t PUBLISH_INTERVAL_MS = 33; // 30 per sec
constexpr uint16_t SENSOR_INTERVAL_MS = 5000;
Timer publishTimer(PUBLISH_INTERVAL_MS);
Timer sensorTimer(SENSOR_INTERVAL_MS);

// ----------- LED wave height indicator
// The general-purpose LED (arduino.led) is dimmed to reflect the current wave height:
// off at/below LED_WAVE_HEIGHT_LOW_CM, full at/above LED_WAVE_HEIGHT_HIGH_CM, and
// linearly interpolated in between.
constexpr float LED_WAVE_HEIGHT_LOW_CM = -10.0f;
constexpr float LED_WAVE_HEIGHT_HIGH_CM = 10.0f;

// ----------- CPU throttling
constexpr uint8_t CPU_FREQUENCY_MHZ = 80; // keep things cool

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

SHT3xTempSensor enclosureTemp;
ESP32TempSensor cpuTemp;

TelemetryEventHandler telemetryHandler(&arduino);
TelemetryPublisher client(TELEMETRY_TOPIC, NUM_DECIMALS, &arduino, &telemetryHandler);
Influx influx(INFLUX_INTERVAL_S, &arduino);
InfluxPoint devicePoint(INFLUX_MEASUREMENT, { { "site", INFLUX_SITE }, { "location", INFLUX_LOCATION }, { "sensor", INFLUX_SENSOR } });
InfluxField* averageDepthField = devicePoint.addValueField("avgDepth", INFLUX_AVG_DEPTH_DECIMALS);

InfluxPoint enclosurePoint(INFLUX_MEASUREMENT, { { "site", INFLUX_SITE }, { "location", INFLUX_LOCATION }, { "sensor", INFLUX_SENSOR }, { "item", "Enclosure" } });
InfluxPoint cpuPoint(INFLUX_MEASUREMENT, { { "site", INFLUX_SITE }, { "location", INFLUX_LOCATION }, { "sensor", INFLUX_SENSOR }, { "item", "CPU" } });
InfluxField* enclosureTempField = enclosurePoint.addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);
InfluxField* enclosureHumidityField = enclosurePoint.addRollingAverageField(INFLUX_ROLLING_SAMPLES, "humidity", INFLUX_DECIMALS);
InfluxField* cpuTempField = cpuPoint.addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);

void setup()
{
   SerialX::begin();
   Serial.println("Wave Publisher");

   // avgDepth isn't posted until the 5 minute averaging window is full (see loop())
   averageDepthField->setEnabled(false);

   arduino.begin(); // sets up the I2C bus/power rail and the RGB status LED
   arduino.setStatus(Status::STARTED);

   // solid on while starting up; switches to wave-height-based fading in loop() once wave data is available
   arduino.led.turnOn(1.0f);

   arduino.initSensor("Enclosure Sensor", []() { return enclosureTemp.begin(); });
   arduino.initSensor("CPU Sensor", []() { return cpuTemp.begin(); });
   arduino.initSensor("Depth Sensor", []() { return depth->begin(); });

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
   // restart every 24 hours to play it safe
   arduino.loop();

   if (client.isStarted())
   {
      // without a delay, the waveshare crashes
      delay(1);

      if (publishTimer.ready())
      {
         float distanceCM = depth->getDepth();
         float waveHeightCM = depth->getWaveHeight();
         Serial.print("Distance: ");
         Serial.print(distanceCM);
         Serial.print(" cm   Wave Height: ");
         Serial.print(waveHeightCM);
         Serial.println(" cm");
         client.setValue(distanceCM);

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

   if (influx.ready())
   {
      devicePoint.post(influx.client(), true);
      enclosurePoint.post(influx.client(), true);
      cpuPoint.post(influx.client(), true);

      // All three points above were only queued into the write buffer (see INFLUX_BATCH_SIZE),
      // so flush now to post them together in a single HTTP request sharing one timestamp.
      if (!influx.client()->flushBuffer())
      {
         Serial.print("InfluxDB flush failed: ");
         Serial.println(influx.client()->getLastErrorMessage());
      }

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

   if (sensorTimer.ready())
   {
      enclosureTempField->set(enclosureTemp.readTemperatureF());
      enclosureHumidityField->set(enclosureTemp.readHumidity());
      cpuTempField->set(cpuTemp.readTemperatureF());
   }

   client.loop(); // Continuously poll for events and maintain connection
}
