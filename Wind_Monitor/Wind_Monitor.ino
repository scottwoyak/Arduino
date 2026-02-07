
#include "Feather.h"
#include "SerialX.h"
#include "WindMeter.h"
#include "Influx.h"
#include "WiFiSettings.h" // for WIFI_SSID and WIFI_PASSWORD
#include "Adafruit_NeoPixel.h"
#include "Bar.h"

Feather feather;
Adafruit_NeoPixel neoPixel(NEOPIXEL_NUM, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

WindMeter wind(A5);

Format speedFormat("##.# mph");

constexpr auto INFLUX_MEASUREMENT = "Wind";
constexpr auto INFLUX_INTERVAL_S = 1;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

TimedPoint windPoint(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT, { {"location", "Studio"} });
Field* windSpeedField = windPoint.addTimeAveragedField("speed", 2);


Color c1 = Color565::fromRGB(0, 128, 0);
Color c2 = Color::YELLOW;
MultiHorizontalBar multiBar(Rect16(0, 135 - 40 - 1, 240, 40), RangeF(0, 40), 4, c1, c2, Color::BLACK);

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
   // get values
   float speed = wind.getSpeed();
   windSpeedField->set(speed);

   // display values
   feather.setCursor(0, 0);
   feather.setTextSize(3);

   feather.println("Wind Monitor", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.setTextSize(3);
   feather.println(speed, speedFormat, Color::VALUE);
   feather.moveCursorY(feather.charH() / 2);

   // display bar
   multiBar.draw(&feather.display, speed);

   // send to influx 
   if (windPoint.ready())
   {
      neoPixel.setBrightness(5);
      neoPixel.setPixelColor(0, 25, 25, 25);
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
