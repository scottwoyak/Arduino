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
//

// Uncomment to use local telemetry server instead of remote
#define TELEMETRY_LOCAL

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
TelemetryPublisher client("Wind/Bragg", NUM_DECIMALS);
Timer serialTimer(SERIAL_INTERVAL_MS);
Timer sensorTimer(SENSOR_INTERVAL_MS);

// ----------- InfluxDB settings
constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_LOCATION = "Bragg";
constexpr uint16_t INFLUX_INTERVAL_S = 60;
constexpr uint8_t INFLUX_DECIMALS = 2;
constexpr size_t INFLUX_ROLLING_SAMPLES = 10;
constexpr uint8_t INFLUX_BATCH_SIZE = 2; // enclosure + CPU temperature points

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
Influx influx(INFLUX_INTERVAL_S, &arduino.status);

TempSensor enclosureTemp;
ESP32TempSensor cpuTemp;

InfluxPoint enclosurePoint(INFLUX_MEASUREMENT, { { "location", INFLUX_LOCATION }, { "item", "Enclosure" } });
InfluxPoint cpuPoint(INFLUX_MEASUREMENT, { { "location", INFLUX_LOCATION }, { "item", "CPU" } });
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
   arduino.status.setStatus(Status::STARTED);

   enclosureTemp.begin();
   cpuTemp.begin();

   wind.begin();

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &arduino.status);
   if (!influx.begin(arduino))
   {
      Util::reset();
   }

   influx.client()->setWriteOptions(WriteOptions().batchSize(INFLUX_BATCH_SIZE).bufferSize(2 * INFLUX_BATCH_SIZE));

   client.setCallbacks(onConnected, onDisconnected, onSendText, onReceiveText, onError, nullptr);
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

