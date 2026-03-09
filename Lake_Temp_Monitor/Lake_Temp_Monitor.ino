#include "Feather.h"
#include "TempSensor.h"
#include "TimedAverager.h"
#include "Stopwatch.h"
#include "Adafruit_SleepyDog.h"
#include "SerialX.h"
#include "Influx.h"
#include <string>
#include "I2CMultiplexor.h"

#include "WiFiSettings.h"

Format humFormat("##.#%");
Format tempFormat("###.## F");

constexpr auto version = "v0.90";

Feather feather;

I2CMultiplexor multi;
TempSensor sensorSurface;
TempSensor sensorBottom;
TempSensor sensor3;
TempSensor sensor4;

constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

TimedPoint pointSurface(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT);
TimedPoint pointBottom(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT);
TimedPoint point3(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT);
TimedPoint point4(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT);

Field* tempSurfaceField = pointSurface.addTimeAveragedField("temperature", 3);
Field* tempBottomField = pointBottom.addTimeAveragedField("temperature", 3);
Field* temp3Field = point3.addTimeAveragedField("temperature", 3);
Field* temp4Field = point4.addTimeAveragedField("temperature", 3);

Field* humSurfaceField = pointSurface.addTimeAveragedField("humidity", 2);
Field* humBottomField = pointBottom.addTimeAveragedField("humidity", 2);
Field* hum3Field = point3.addTimeAveragedField("humidity", 2);
Field* hum4Field = point4.addTimeAveragedField("humidity", 2);


constexpr uint8_t NUM_SENSORS = 4;
TempSensor* sensors[] = { &sensorSurface, &sensorBottom, &sensor3, &sensor4 };
TimedPoint* points[] = { &pointSurface, &pointBottom, &point3, &point4 };
Field* tempFields[] = { tempSurfaceField, tempBottomField, temp3Field, temp4Field };
Field* humFields[] = { humSurfaceField, humBottomField, hum3Field, hum4Field };
uint8_t sensorPorts[] = { 2, 3, 4, 5 };

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

   multi.begin();

   feather.print("Sensors... ", Color::LABEL);
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      multi.select(sensorPorts[i]);
      if (sensors[i]->begin(true))
      {
         SerialX::println("   Type: ", sensors[i]->type());
         SerialX::println("   Address: ", sensors[i]->address());
         SerialX::println("   ID: ", sensors[i]->id());
      }
      else
      {
         feather.printR("FAILED", Color::RED);
         while (1);
      }
   }
   feather.printlnR("ok", Color::VALUE);


   Influx::begin(&feather, WIFI_SSID, WIFI_PASSWORD, &client);

   delay(5000);

   pointSurface.addTag("location", "Surface");
   pointBottom.addTag("location", "Bottom");

   feather.clearDisplay();
   feather.echoToSerial = false;

   Watchdog.enable(80 * 1000);
}

void loop()
{
   Watchdog.reset();

   // Store measured value into point
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      multi.select(sensorPorts[i]);
      tempFields[i]->set(sensors[i]->readTemperatureF());
      humFields[i]->set(sensors[i]->readHumidity());
   }

   // Check WiFi connection and reconnect if needed
   if (wifiMulti.run() != WL_CONNECTED)
   {
      feather.println("Wifi connection lost");
   }

   feather.setCursor(0, 0);
   feather.display.setTextSize(2);
   feather.print("Influx", Color::HEADING);
   feather.println();

   feather.setTextSize(3);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      float temp = tempFields[i]->get();
      float hum = humFields[i]->get();

      feather.print(temp, tempFormat, Color::VALUE);
      feather.printR(hum, humFormat, Color::VALUE);
      feather.println();
   }

   feather.setTextSize(2);
   feather.printR(version, Color::SUB_LABEL);

   // Write point
   if (pointSurface.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);

      bool failed = false;
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (points[i]->post(&client) == false)
         {
            failed = true;
            Serial.println("InfluxDB write failed: ");
            Serial.println(client.getLastErrorMessage());
         }
      }

      if (failed)
      {
         // sleep for 60 seconds (reboot upon wake up)
         feather.deepSleep(60);
      }

      digitalWrite(BUILTIN_LED, LOW);
   }
}


