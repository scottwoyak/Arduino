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

   feather.display.print("WiFi...");
   Serial.print("WiFi...");
   while (wifiMulti.run() != WL_CONNECTED)
   {
      Serial.print(".");
      delay(100);
   }
   feather.display.println(" ok");


   // Accurate time is necessary for certificate validation and writing in batches
   // We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
   // Syncing progress and the time will be printed to Serial.
   feather.display.print("Syncing Time...");
   timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
   feather.display.println(" ok");


   // Check server connection
   feather.display.print("InfluxDB...");

   if (client.validateConnection())
   {
      feather.display.println(" ok");
      //feather.display.println(client.getServerUrl());
   }
   else
   {
      feather.display.println("failed");
      feather.display.println(client.getLastErrorMessage());
      while (1);
   }

   sensor = TempSensorFactory::create();
   feather.display.print("Sensor.begin()");
   if (sensor->begin())
   {
      feather.display.println(" ok");
   }
   else
   {
      feather.display.println(" failed");
      while (1);
   }

   delay(2000);

   point.addTag("location", "studio");

   feather.display.fillScreen(ST77XX_BLACK);
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


