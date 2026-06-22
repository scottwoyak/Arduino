#include "Feather_ESP32_S3.h"
#include "TempSensor.h"
#include "Adafruit_SleepyDog.h"
#include "SerialX.h"
#include "Influx.h"
#include <string>
#include <Timer.h>

#include "WiFiSettings.h" // for WIFI_SSID and WIFI_PASSWORD

constexpr const char* location = "StudioTable";

Format humFormat("##.#%");
Format tempFormat("###.## F");

constexpr auto version = "v1.0";

Feather_ESP32_S3 feather;
NeoPixelStatus status(&feather.neoPixel);

TempSensor sensor;

constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
SimplePoint point(INFLUX_MEASUREMENT); // Influx data point
Field* tempField = point.addTimeAveragedField(INFLUX_INTERVAL_S, "temperature", 3);
Field* humField = point.addTimeAveragedField(INFLUX_INTERVAL_S, "humidity", 2);
Timer influxTimer(INFLUX_INTERVAL_S * 1000);

void setup()
{
   Wire.begin();
   SerialX::begin();

   feather.begin();
   feather.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   status.begin();
   status.setLevel(0.05f);

   feather.echoToSerial = true;
   feather.clearDisplay();
   feather.setTextSize(2);
   feather.println("Initializing", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.print("Loc: ", Color::LABEL);
   feather.printlnR(location, Color::VALUE);

   feather.print("Sensor... ", Color::LABEL);
   if (sensor.begin(true))
   {
      feather.printlnR("ok", Color::VALUE);

      Serial.print("   Type: ");
      Serial.println(sensor.type());
      Serial.print("   Address: ");
      Serial.println(sensor.address());
      Serial.print("   ID: ");
      Serial.println(sensor.id());
   }
   else
   {
      feather.printR("FAILED", Color::RED);
      while (1);
   }

   Influx::begin(&feather, WIFI_SSID, WIFI_PASSWORD, &client, &status);

   delay(1000);

   point.addTag("location", location);

   feather.clearDisplay();
   feather.echoToSerial = false;

   Watchdog.enable(60 * 1000);
}

uint count = 0;

void loop()
{
   Watchdog.reset();

   count++;

   // Store measured value into point
   tempField->set(sensor.readTemperatureF());
   humField->set(sensor.readHumidity());

   // Check WiFi connection and reconnect if needed
   if (wifiMulti.run() != WL_CONNECTED)
   {
      feather.println("Wifi connection lost");
   }

   feather.setCursor(0, 0);
   feather.setTextSize(2);
   feather.print("Influx", Color::HEADING);
   feather.printR(sensor.type(), Color::GRAY);
   feather.println();

   float temp = tempField->get();
   float hum = humField->get();

   feather.setTextSize(4);
   constexpr auto MAX_CHARS = 8;
   uint8_t x = (feather.display.width() - MAX_CHARS * feather.charW()) / 2;
   constexpr auto SPACING = 8;
   feather.setCursor(x, (feather.display.height() - 2 * feather.charH()) / 2 - SPACING / 2);
   feather.println(temp, tempFormat, Color::VALUE);

   feather.setCursor(x, feather.display.getCursorY() + SPACING);
   feather.println(hum, humFormat, Color::VALUE);

   feather.setTextSize(2);
   feather.setCursor(0, -feather.charH());
   feather.print(location, Color::CYAN);

   feather.printR(version, Color::SUB_LABEL);

   // Write point
   if (influxTimer.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      if (point.post(&client) == false)
      {
         // sleep for 60 seconds (reboot upon wake up)
         feather.deepSleep(60);
      }
      digitalWrite(BUILTIN_LED, LOW);

      count = 0;
   }

   // reading the sensor too often can actually cause it to heat up and produce inaccurate readings
   delay(500);
}


