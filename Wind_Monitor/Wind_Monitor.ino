
#include "Feather.h"
#include "SerialX.h"
#include "WindMeter.h"
#include "Influx.h"
#include "WiFiSettings.h" // for WIFI_SSID and WIFI_PASSWORD
#include "Adafruit_NeoPixel.h"

Feather feather;
Adafruit_NeoPixel neoPixel(NEOPIXEL_NUM, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

WindMeter wind(A5);

Format speedFormat("##.# mph");

constexpr auto INFLUX_MEASUREMENT = "Wind";
constexpr auto INFLUX_INTERVAL_S = 1;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

TimedPoint windPoint(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT, { {"location", "Studio"} });
Field* windSpeedField = windPoint.addTimeAveragedField("speed", 2);

void setup()
{
   SerialX::begin();
   feather.begin();
   wind.begin();
   neoPixel.begin();

   Influx::startInit(&feather);
   Influx::begin(&feather, WIFI_SSID, WIFI_PASSWORD, &client);
   Influx::endInit(&feather);
}

void loop()
{
   feather.setCursor(0, 0);
   feather.setTextSize(3);

   feather.println("Wind Monitor", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   float speed = wind.getSpeed();
   windSpeedField->set(speed);


   feather.setTextSize(3);
   feather.println(speed, speedFormat, Color::VALUE);

   uint16_t barHeight = 40;
   float max = 10.0;
   uint16_t length = constrain(speed / max, 0, 1) * feather.display.width();
   feather.display.fillRect(0, feather.display.height() - barHeight-1, length, barHeight, (uint16_t)Color::GREEN);
   feather.display.fillRect(length, feather.display.height() - barHeight -1, feather.display.width() - length, barHeight, (uint16_t)Color::BLACK);

   if (windPoint.ready())
   {
      neoPixel.setBrightness(5);
      neoPixel.setPixelColor(0, 0, 0, 255);
      neoPixel.show();
      if (windPoint.post(&client) == false)
      {
         Serial.println("InfluxDB write failed: ");
         Serial.println(client.getLastErrorMessage());
      }
      neoPixel.setBrightness(0);
      neoPixel.show();
   }
}
