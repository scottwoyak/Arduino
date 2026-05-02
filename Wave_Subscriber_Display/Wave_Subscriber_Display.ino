// undefine to use the remote server
//#define TELEMETRY_LOCAL

#include <WiFiMulti.h>

#include "Feather.h"
#include "SerialX.h"
#include "WiFiSettings.h"
#include "TimedAverager.h"
#include "BarChart.h"
#include "RateTracker.h"
#include "TelemetryClient.h"

Feather feather;
WiFiMulti wifi;
RollingRateTracker refreshRate(100);
Stopwatch sw;
TelemetrySubscriber client("Waves/Lake");

Format heightFormat("###.# cm");

constexpr uint16_t DISPLAY_HEIGHT = 135;
constexpr uint16_t DISPLAY_WIDTH = 240;
constexpr uint16_t HEADER_HEIGHT = 3 * 8 + 4; // one line of text size 3 plus padding

Color LakeBlue = Color565::fromRGB(0, 0, 255);
constexpr RangeF ROLLING_RANGE = { 0, 150 };
constexpr Rect16 ROLLING_RECT(0, HEADER_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT);
RollingBarChart rollingChart(ROLLING_RECT, ROLLING_RANGE, LakeBlue, Color::BLACK);

void setup()
{
   heightFormat.alignment = Format::Alignment::RIGHT;

   SerialX::begin();
   feather.begin();

   // Connect to WiFi
   wifi.addAP(WIFI_SSID, WIFI_PASSWORD);

   feather.setTextSize(2);
   feather.setCursorY(-feather.charH());
   feather.println("Wave Subscriber", Color::GRAY);

   feather.println("Initializing", Color::HEADING2);
   feather.moveCursorY(4);

   feather.print("WiFi...", Color::LABEL);
   while (wifi.run() != WL_CONNECTED)
   {
      feather.print(".", Color::LABEL);
   }
   feather.printlnR("OK", Color::VALUE);
   feather.moveCursorY(1);

   feather.print("WebSocket...", Color::LABEL);

   client.setCallbacks(nullptr, onDisconnected, nullptr, nullptr, onError, onStarted);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);


   delay(1000); // provide time for the wind meter to get a reading

   sw.reset();
}

void onStarted()
{
   feather.clearDisplay();
}

void onError(std::string msg)
{
   feather.setTextSize(2);
   feather.clearDisplay();
   feather.display.setTextWrap(true);
   feather.println(msg, Color::WHITE, Color::RED);
   Util::reset(10);
}

void onDisconnected()
{
   Util::reset();
}

void loop()
{
   client.loop();

   if (client.isStarted() == false)
   {
      return;
   }

   refreshRate.tick();

   // get value measured from the bottom of the graph
   float height = ROLLING_RANGE.max - client.getValue();

   if (isnan(height))
   {
      height = ROLLING_RANGE.max;
   }

   rollingChart.set(height);

   // display values
   feather.setCursor(0, 0);
   feather.setTextSize(3);

   feather.setTextSize(2);
   feather.print(client.getTopic(), Color::HEADING);  
   feather.setTextSize(3);
   feather.printR(height, heightFormat, Color::VALUE);
   feather.moveCursorY(4);

   rollingChart.draw(&feather.display);

   if (sw.elapsedSecs() > 1)
   {
      Serial.println(refreshRate.getRate());
      Serial.println(height);
      sw.reset();
   }
}

