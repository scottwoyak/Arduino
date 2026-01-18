#include "Feather_ESP32_S3.h"
#include "TempSensor.h"

#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

// for WIFI_SSID and WIFI_PASSWORD
#include "WiFiSettings.h"

#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN "0kLrOsYRO78T-dFwOV26MG0B6pO0BFLGsTlq4p93EAE2Jtm_5TJgxNzwgyjlTMXVhwvXvw8M7XGw_vfXv7Mu5A=="
#define INFLUXDB_ORG "2f1afc3c5db17a8c"
#define INFLUXDB_BUCKET "temperature"

// Time zone info
#define TZ_INFO "UTC-5"

Feather_ESP32_S3 feather;
ITempSensor* sensor;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Data point
Point point("Temperature");

// The setup() function runs once each time the micro-controller starts
void setup()
{
   Wire.begin();

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

   feather.begin();

   // Setup wifi
   WiFi.mode(WIFI_STA);
   wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

   const int16_t MSG_COLOR = WHITE;
   const int16_t OK_COLOR = YELLOW;
   const int16_t FAILED_COLOR = ORANGE;
   const int16_t CHAR_WIDTH = 12;

   feather.display.setTextWrap(false);
   feather.print("WiFi... ", MSG_COLOR);
   while (wifiMulti.run() != WL_CONNECTED)
   {
      Serial.print(".");
      delay(100);
   }
   feather.display.setCursor(feather.display.width() - 2 * CHAR_WIDTH, feather.display.getCursorY());
   feather.println("ok", OK_COLOR);


   // Accurate time is necessary for certificate validation and writing in batches
   // We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
   // Syncing progress and the time will be printed to Serial.
   feather.print("Syncing Time... ", MSG_COLOR);
   timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
   feather.display.setCursor(feather.display.width() - 2 * CHAR_WIDTH, feather.display.getCursorY());
   feather.println("ok", OK_COLOR);

   // Check server connection
   feather.print("InfluxDB... ", MSG_COLOR);

   if (client.validateConnection())
   {
      feather.display.setCursor(feather.display.width() - 2 * CHAR_WIDTH, feather.display.getCursorY());
      feather.println("ok", OK_COLOR);
      //feather.display.println(client.getServerUrl());
   }
   else
   {
      feather.display.setCursor(feather.display.width() - 6 * CHAR_WIDTH, feather.display.getCursorY());
      feather.println("FAILED", FAILED_COLOR);
      feather.display.setTextSize(1);
      feather.println(client.getLastErrorMessage(), FAILED_COLOR);
      while (1);
   }

   feather.print("Sensor... ", MSG_COLOR);
   sensor = TempSensorFactory::create(false);
   if (sensor->exists())
   {
      feather.display.setCursor(feather.display.width() - 2 * CHAR_WIDTH, feather.display.getCursorY());
      feather.println("ok", OK_COLOR);
      feather.print("Sensor: ");
      String info = sensor->info();
      feather.display.setCursor(feather.display.width() - info.length() * CHAR_WIDTH, feather.display.getCursorY());
      feather.println(sensor->info(), OK_COLOR);
   }
   else
   {
      feather.display.setCursor(feather.display.width() - 6 * CHAR_WIDTH, feather.display.getCursorY());
      feather.print("FAILED", FAILED_COLOR);
      while (1);
   }

   feather.print("Sensor.begin() ", MSG_COLOR);
   if (sensor->begin())
   {
      feather.display.setCursor(feather.display.width() - 2 * CHAR_WIDTH, feather.display.getCursorY());
      feather.println("ok", OK_COLOR);
   }
   else
   {
      feather.display.setCursor(feather.display.width() - 6 * CHAR_WIDTH, feather.display.getCursorY());
      feather.println("FAILED", FAILED_COLOR);
      while (1);
   }

   delay(5000);

   point.addTag("location", "studio2");

   feather.display.fillScreen(BLACK);
}

// Add the main program code into the continuous loop() function
void loop()
{
   feather.display.setTextSize(2);
   feather.display.setCursor(0, 0);
   // Clear fields for reusing the point. Tags will remain the same as set above.
   point.clearFields();

   // Store measured value into point
   float tempF = sensor->readTemperatureF();
   point.addField("temperature", tempF);

   // Print what are we exactly writing
   Serial.println(point.toLineProtocol());

   // Check WiFi connection and reconnect if needed
   if (wifiMulti.run() != WL_CONNECTED)
   {
      feather.display.println("Wifi connection lost");
   }

   // Write point
   if (!client.writePoint(point))
   {
      feather.display.print("InfluxDB write failed: ");
      feather.display.println(client.getLastErrorMessage());
      Serial.println(client.getLastErrorMessage());
   }
   else
   {
      feather.display.setTextSize(4);
      feather.display.setCursor(0, 0);
      feather.display.print(sensor->readTemperatureF());
      feather.display.print(" F");
   }

   delay(1000);
}


