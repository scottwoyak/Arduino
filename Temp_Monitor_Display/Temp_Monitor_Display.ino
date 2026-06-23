//-------------------------------------------------------------------------------------------------
//
// Displays the current temperature and humidity, samples the sensor every 500 ms,
// and periodically uploads averaged readings to InfluxDB.
//
//-------------------------------------------------------------------------------------------------
#include "Feather_ESP32_S3.h"
#include "TempSensor.h"
#include <Adafruit_SleepyDog.h>
#include "SerialX.h"
#include "Influx.h"
#include "Timer.h"

#include "WiFiSettings.h"

constexpr const char* location = "StudioTable";
constexpr auto version = "v1.0";
constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15;
constexpr auto SENSOR_INTERVAL_MS = 250;
constexpr auto WATCHDOG_INTERVAL_MS = 60 * 1000;
constexpr auto MAX_CHARS = 8;
constexpr auto SPACING = 8;
constexpr auto SENSOR_POST_FAILURE_SLEEP_S = 60;
constexpr uint8_t TEXT_SIZE_SMALL = 2;
constexpr uint8_t TEXT_SIZE_LARGE = 4;
constexpr uint8_t INFLUX_TEMP_DECIMAL_PLACES = 3;
constexpr uint8_t INFLUX_HUMIDITY_DECIMAL_PLACES = 2;

Format humFormat("##.#%");
Format tempFormat("###.## F");

Feather_ESP32_S3 feather;
NeoPixelStatus status(&feather.neoPixel);
TempSensor sensor;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
SimplePoint point(INFLUX_MEASUREMENT);
Field* tempField = point.addTimeAveragedField(INFLUX_INTERVAL_S, "temperature", INFLUX_TEMP_DECIMAL_PLACES);
Field* humField = point.addTimeAveragedField(INFLUX_INTERVAL_S, "humidity", INFLUX_HUMIDITY_DECIMAL_PLACES);
Timer sensorTimer(SENSOR_INTERVAL_MS);
Timer influxTimer(INFLUX_INTERVAL_S * 1000);

void setup()
{
   Wire.begin();
   SerialX::begin();

   feather.begin();
   feather.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   status.begin();

   feather.echoToSerial = true;
   feather.clearDisplay();
   feather.setTextSize(TEXT_SIZE_SMALL);
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

   Watchdog.enable(WATCHDOG_INTERVAL_MS);
}

void loop()
{
   Watchdog.reset();

   if (sensorTimer.ready())
   {
      tempField->set(sensor.readTemperatureF());
      humField->set(sensor.readHumidity());
   }

   if (WiFi.status() != WL_CONNECTED)
   {
      feather.println("Wifi connection lost");
   }

   feather.setCursor(0, 0);
   feather.setTextSize(TEXT_SIZE_SMALL);
   feather.print("Influx", Color::HEADING);
   feather.printR(sensor.type(), Color::GRAY);
   feather.println();

   float temp = tempField->average();
   float hum = humField->average();
   uint8_t x = (feather.display.width() - MAX_CHARS * feather.charW()) / 2;

   feather.setTextSize(TEXT_SIZE_LARGE);
   feather.setCursor(x, (feather.display.height() - 2 * feather.charH()) / 2 - SPACING / 2);
   feather.println(temp, tempFormat, Color::VALUE);

   feather.setCursor(x, feather.display.getCursorY() + SPACING);
   feather.println(hum, humFormat, Color::VALUE);

   feather.setTextSize(TEXT_SIZE_SMALL);
   feather.setCursor(0, -feather.charH());
   feather.print(location, Color::CYAN);
   feather.printR(version, Color::SUB_LABEL);

   if (influxTimer.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      if (!point.post(&client))
      {
         feather.deepSleep(SENSOR_POST_FAILURE_SLEEP_S);
      }
      digitalWrite(BUILTIN_LED, LOW);
   }
}


