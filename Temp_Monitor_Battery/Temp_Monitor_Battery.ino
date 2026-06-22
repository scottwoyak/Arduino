#include "Feather_ESP32_S3.h"
#include "TempSensor.h"
#include "Adafruit_SleepyDog.h"
#include "Adafruit_MAX1704x.h"
#include "SerialX.h"
#include "Influx.h"


#include "WiFiSettings.h" // for WIFI_SSID and WIFI_PASSWORD

constexpr auto version = "v0.91";


Feather_ESP32_S3 feather;
TempSensor sensor;
Adafruit_MAX17048 battery;

constexpr auto INFLUX_INTERVAL_S = 60;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
SimplePoint airPoint("Air"); // Influx data point
SimplePoint powerPoint("Power"); // Influx data point
Field* tempField = airPoint.addValueField("temperature", 2);
Field* humField = airPoint.addValueField("humidity", 2);
Field* voltsField = powerPoint.addValueField("volts", 2);

void goToSleep()
{
   Serial.println("Going to sleep for 60 seconds...");
   Serial.println();
   Serial.println();
   delay(100); // let serial finish

   // sleep for 60 seconds (reboot upon wake up)
   feather.deepSleep(INFLUX_INTERVAL_S);
}

void setup()
{
   Wire.begin();
   SerialX::begin();
   Serial.println("Initializing... ");

   feather.begin();
   pinMode(BUILTIN_LED, OUTPUT);
   digitalWrite(BUILTIN_LED, HIGH);

   // turn off power stuff
   pinMode(NEOPIXEL_POWER, OUTPUT);
   digitalWrite(NEOPIXEL_POWER, LOW);
   pinMode(TFT_BACKLITE, OUTPUT);
   digitalWrite(TFT_BACKLITE, LOW);

   feather.print("Sensor... ");
   if (sensor.begin(true))
   {
      Serial.println("ok");
   }
   else
   {
      Serial.print("FAILED");
      goToSleep();
   }

   feather.print("Battery... ");
   if (battery.begin())
   {
      Serial.println("ok");
   }
   else
   {
      Serial.print("FAILED");
      goToSleep();
   }

   Influx::begin(&feather, WIFI_SSID, WIFI_PASSWORD, &client);

   airPoint.addTag("location", "Test");
   powerPoint.addTag("location", "Test");

   Watchdog.enable(60 * 1000);
}

// Add the main program code into the continuous loop() function
void loop()
{
   Watchdog.reset();

   // Store measured value into point
   tempField->set(sensor.readTemperatureF());
   humField->set(sensor.readHumidity());
   voltsField->set(battery.cellVoltage());

   // Write points
   Serial.println("Writing data points...");
   if (airPoint.post(&client, true)==false)
   {
      Serial.println("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
   }

   if (powerPoint.post(&client, true) == false)
   {
      Serial.println("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
   }

   goToSleep();
}


