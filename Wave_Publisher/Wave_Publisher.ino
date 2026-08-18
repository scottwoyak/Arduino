//
// Wave Publisher
//
// Reads water depth from an ultrasonic (or MS5837 pressure) sensor and publishes live
// wave height readings over a WebSocket telemetry connection.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   streams live wave height readings as they're read.
// - Wave height is the offset of the current depth reading from a running average of
//   readings taken over the past several minutes (see DepthSensorBase).
// - Prints distance and wave height readings to Serial every publish cycle.
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

// Uncomment to use local telemetry server instead of remote
#define TELEMETRY_LOCAL

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "DepthSensorBase.h"
#include "ESP32TempSensor.h"
#include "Influx.h"
#include "SerialX.h"
#include "SHT3xTempSensor.h"
#include "Status.h"
#include "Stopwatch.h"
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
constexpr uint16_t SENSOR_INTERVAL_MS = 5000;
TelemetryPublisher client("Waves/Test", NUM_DECIMALS);
//TelemetryPublisher client("Waves/Lake", NUM_DECIMALS);
Timer publishTimer(33); // 30 per sec
Timer sensorTimer(SENSOR_INTERVAL_MS);

// ----------- LED wave height indicator
// The general-purpose LED (arduino.led) is dimmed to reflect the current wave height:
// off at/below LED_WAVE_HEIGHT_LOW_CM, full at/above LED_WAVE_HEIGHT_HIGH_CM, and
// linearly interpolated in between.
constexpr float LED_WAVE_HEIGHT_LOW_CM = -10.0f;
constexpr float LED_WAVE_HEIGHT_HIGH_CM = 10.0f;

// ----------- InfluxDB settings
constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_LOCATION = "Test";
constexpr uint16_t INFLUX_INTERVAL_S = 60;
constexpr uint8_t INFLUX_DECIMALS = 1;
constexpr size_t INFLUX_ROLLING_SAMPLES = 10;
constexpr uint8_t INFLUX_BATCH_SIZE = 3; // depth + enclosure + CPU temperature points

// ----------- CPU throttling
constexpr uint8_t CPU_FREQUENCY_MHZ = 80; // keep things cool

Stopwatch restartSW;

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring. arduino.status drives both the external RGB LED and the
// onboard NeoPixel, so status is visible even when the external LED isn't plugged in.
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

Influx influx(INFLUX_INTERVAL_S, &arduino.status);
InfluxPoint depthPoint(INFLUX_MEASUREMENT, { { "location", INFLUX_LOCATION } });
InfluxField* averageDepthField = depthPoint.addValueField("averageDepth", INFLUX_DECIMALS);

InfluxPoint enclosurePoint(INFLUX_MEASUREMENT, { { "location", INFLUX_LOCATION }, { "item", "Enclosure" } });
InfluxPoint cpuPoint(INFLUX_MEASUREMENT, { { "location", INFLUX_LOCATION }, { "item", "CPU" } });
InfluxField* enclosureTempField = enclosurePoint.addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);
InfluxField* enclosureHumidityField = enclosurePoint.addRollingAverageField(INFLUX_ROLLING_SAMPLES, "humidity", INFLUX_DECIMALS);
InfluxField* cpuTempField = cpuPoint.addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);

void setup()
{
   SerialX::begin();
   Serial.println("Wave Publisher");

   arduino.begin(); // sets up the I2C bus/power rail and the RGB status LED
   arduino.status.setStatus(Status::STARTED);

   // solid on while starting up; switches to wave-height-based fading once started (see onStarted())
   arduino.led.setLevel(1.0f);
   arduino.led.turnOn();

   arduino.initSensor("Enclosure Sensor", []() { return enclosureTemp.begin(); });
   arduino.initSensor("CPU Sensor", []() { return cpuTemp.begin(); });
   arduino.initSensor("Depth Sensor", []() { return depth->begin(); });

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &arduino.status);
   if (!influx.begin(arduino))
   {
      arduino.status.setStatus(Status::FAILED);
      delay(1000); // time for LED to show
      Util::reset();
   }

   influx.client()->setWriteOptions(WriteOptions().batchSize(INFLUX_BATCH_SIZE).bufferSize(2 * INFLUX_BATCH_SIZE));

   client.setCallbacks(onConnected, onDisconnected, onSendText, onReceiveText, onError, onStarted);
   arduino.beginClient("WebSocket", []() { client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &arduino.status);

   setCpuFrequencyMhz(CPU_FREQUENCY_MHZ);
}

///
/// <summary>
/// Invoked when the telemetry WebSocket connection is established.
/// </summary>
///
void onConnected()
{
   Serial.println("Connected");
}

///
/// <summary>
/// Invoked when the telemetry client finishes starting up.
/// </summary>
///
void onStarted()
{
   arduino.status.setStatus(Status::READY);
}

///
/// <summary>
/// Invoked when the telemetry WebSocket connection is lost. Resets the device so it
/// re-establishes a fresh connection on restart.
/// </summary>
/// <param name="reason">Reason for the disconnect, as reported by the telemetry client</param>
///
void onDisconnected(std::string reason)
{
   Serial.println("Disconnected: " + String(reason.c_str()));
   arduino.status.setStatus(Status::FAILED);
   delay(1000); // time for Serial to print and LED to show
   Util::reset();
}

///
/// <summary>
/// Invoked when the telemetry client reports an error. Resets the device so it can
/// attempt to recover with a fresh connection.
/// </summary>
/// <param name="msg">Error message reported by the telemetry client</param>
///
void onError(std::string msg)
{
   Serial.print("Error: ");
   Serial.println(msg.c_str());
   arduino.status.setStatus(Status::FAILED);
   delay(1000); // time for Serial to print and LED to show
   Util::reset();
}

///
/// <summary>
/// Replaces all occurrences of a substring within a string, in place.
/// </summary>
/// <param name="str">String to modify</param>
/// <param name="from">Substring to search for</param>
/// <param name="to">Replacement substring</param>
///
void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
   if (from.empty())
   {
      return;
   }

   size_t startPos = 0;
   while ((startPos = str.find(from, startPos)) != std::string::npos)
   {
      str.replace(startPos, from.length(), to);
      startPos += to.length(); // Move past the new replacement
   }
}

///
/// <summary>
/// Invoked when the telemetry client sends a text message. Logs the message to Serial
/// with escaped newlines for readability.
/// </summary>
/// <param name="msg">The text message that was sent</param>
///
void onSendText(std::string msg)
{
   Serial.print(">>> ");
   replaceAll(msg, "\n", "\\n");
   msg = '"' + msg + '"';
   Serial.println(msg.c_str());
}

///
/// <summary>
/// Invoked when the telemetry client receives a text message. Logs the message to Serial
/// with escaped newlines for readability.
/// </summary>
/// <param name="msg">The text message that was received</param>
///
void onReceiveText(std::string msg)
{
   Serial.print("<<< ");
   replaceAll(msg, "\n", "\\n");
   msg = '"' + msg + '"';
   Serial.println(msg.c_str());
}

void loop()
{
   // restart every 24 hours to play it safe
   if (restartSW.elapsedSecs() > 24 * 60 * 60)
   {
      Util::reset();
   }

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
         client.setValue(waveHeightCM);

         float ledLevel = (waveHeightCM - LED_WAVE_HEIGHT_LOW_CM) / (LED_WAVE_HEIGHT_HIGH_CM - LED_WAVE_HEIGHT_LOW_CM);
         arduino.led.setLevel(constrain(ledLevel, 0.0f, 1.0f));
         arduino.led.turnOn();

         averageDepthField->set(depth->getAverageDepth());
      }
   }

   if (influx.ready())
   {
      depthPoint.post(influx.client(), true);
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
