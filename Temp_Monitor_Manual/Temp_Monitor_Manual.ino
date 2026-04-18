#include "Feather_ESP32_S3.h"
#include "IndexedEncoder.h"
#include "TempSensor.h"
#include "TimedAverager.h"
#include "Stopwatch.h"
#include "Adafruit_SleepyDog.h"
#include "SerialX.h"
#include "Influx.h"
#include <string>

#include "WiFiSettings.h" // for WIFI_SSID and WIFI_PASSWORD

Format humFormat("##.#%");
Format tempFormat("###.## F");

constexpr auto version = "v0.96";

Feather_ESP32_S3 feather;
String location = "Test 1";

TempSensor sensor;

constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
TimedPoint point(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT); // Influx data point
Field* tempField = point.addTimeAveragedField("temperature", 3);
Field* humField = point.addTimeAveragedField("humidity", 2);

void setup()
{
   Wire.begin();
   SerialX::begin();

   feather.begin();
   feather.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   feather.echoToSerial = true;
   feather.clearDisplay();
   feather.println("Initializing", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.print("Sensor... ", Color::LABEL);
   if (sensor.begin(true))
   {
      feather.printlnR("ok", Color::VALUE);

      SerialX::println("   Type: ", sensor.type());
      SerialX::println("   Address: ", sensor.address());
      SerialX::println("   ID: ", sensor.id());
   }
   else
   {
      feather.printR("FAILED", Color::RED);
      while (1);
   }

   Influx::begin(&feather, WIFI_SSID, WIFI_PASSWORD, &client);

   delay(5000);

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
   feather.print(location.c_str(), Color::CYAN);

   feather.printR(version, Color::SUB_LABEL);

   // Write point
   if (point.ready())
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
}


