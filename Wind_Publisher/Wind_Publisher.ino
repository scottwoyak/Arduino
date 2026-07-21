
//
// Wind Publisher
//

#include <WiFi.h>
#include "SerialX.h"
#include "WiFiSettings.h"
#include "TelemetryClient.h"
#include "Url.h"
#include "WindMeter.h"
#include "Status.h"
#include <Timer.h>
#include <Wire.h>
#include "TempSensor.h"
#include "ESP32TempSensor.h"
#include "Influx.h"

Timer serialTimer(5000);
Timer sensorTimer(100);
constexpr uint8_t NUM_DECIMALS = 2;
TelemetryPublisher client("Wind/Bragg", NUM_DECIMALS);

// InfluxDB settings
constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_LOCATION = "Bragg";
constexpr auto INFLUX_INTERVAL_S = 60;
constexpr uint8_t INFLUX_DECIMALS = 2;
constexpr size_t INFLUX_ROLLING_SAMPLES = 10;

// Wind sensor and LED pins
constexpr auto WIND_SENSOR_PIN = 1;
constexpr auto WIND_LED_PIN = 6;


// I2C pins (custom configuration)
constexpr auto I2C_SDA_PIN = 11;
constexpr auto I2C_SCL_PIN = 12;

constexpr auto RED_LED_PIN = 10;
constexpr auto BLUE_LED_PIN = 9;
constexpr auto GREEN_LED_PIN = 8;


WindMeter wind(WIND_SENSOR_PIN, WIND_LED_PIN);


RGBLEDStatus status(RED_LED_PIN, GREEN_LED_PIN, BLUE_LED_PIN);
//NeoPixelStatus status;

TempSensor enclosureTemp;
ESP32TempSensor cpuTemp;

InfluxDBClient influxClient(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
InfluxPoint enclosureTempPoint(INFLUX_MEASUREMENT, { { "location", INFLUX_LOCATION }, { "item", "Enclosure" } });
InfluxPoint cpuTempPoint(INFLUX_MEASUREMENT, { { "location", INFLUX_LOCATION }, { "item", "CPU" } });
InfluxField* enclosureTempField = enclosureTempPoint.addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);
InfluxField* enclosureHumidityField = enclosureTempPoint.addRollingAverageField(INFLUX_ROLLING_SAMPLES, "humidity", INFLUX_DECIMALS);
InfluxField* cpuTempField = cpuTempPoint.addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);
Timer influxTimer(INFLUX_INTERVAL_S * 1000);

void setup()
{
   Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
   SerialX::begin();
   Serial.println("Wind Publisher");


   status.begin();
   status.setStatus(Status::STARTED);

   enclosureTemp.begin();
   cpuTemp.begin();

   wind.begin();

   // Connect to WiFi
   status.setStatus(Status::WIFI_CONNECTING);
   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
   Serial.print("WiFi...");
   while (WiFi.status() != WL_CONNECTED)
   {
      Serial.print(".");
      delay(500);
   }
   Serial.println("OK");

   status.setStatus(Status::WEB_CONNECTING);
   client.setCallbacks(onConnected, onDisconnected, onSendText, onReceiveText, onError, nullptr);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);

   setCpuFrequencyMhz(80); // keep things cool  
}

void onConnected()
{
   Serial.println("Connected");
   status.setStatus(Status::READY);
}

void onDisconnected()
{
   Serial.println("Disconnected");
   delay(1000); // time for Serial to print and LED to show
   Util::reset();
}

void onError(std::string msg)
{
   Serial.print("Error: ");
   Serial.println(msg.c_str());
   delay(1000); // time for Serial to print and LED to show
   Util::reset();
}

void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
   if (from.empty()) return;
   size_t start_pos = 0;
   while ((start_pos = str.find(from, start_pos)) != std::string::npos)
   {
      str.replace(start_pos, from.length(), to);
      start_pos += to.length(); // Move past the new replacement
   }
}

void onSendText(std::string msg)
{
   Serial.print(">>> ");
   replaceAll(msg, "\n", "\\n");
   msg = '"' + msg + '"';
   Serial.println(msg.c_str());
}

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

      Serial.print("CPU temp: "); 
      Serial.print(cpuTemp.readTemperatureF());
      Serial.println(" °F");
   }

   if (client.isStarted() && influxTimer.ready())
   {
      enclosureTempPoint.post(&influxClient, true);
      cpuTempPoint.post(&influxClient, true);
   }
}
