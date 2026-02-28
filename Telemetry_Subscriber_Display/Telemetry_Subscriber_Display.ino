

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>

#include "Feather.h"
#include "SerialX.h"
#include "WiFiSettings.h"
#include "Stopwatch.h"
#include "RateTracker.h"
#include "Url.h"

#include <TelemetryClient.h>

Feather feather;

//constexpr auto HOST = TELEMETRY_LOCAL_HOST;
//constexpr auto PORT = TELEMETRY_LOCAL_PORT;
constexpr auto HOST = TELEMETRY_REMOTE_HOST;
constexpr auto PORT = TELEMETRY_REMOTE_PORT;

WiFiMulti WiFiMulti;
Stopwatch sw(false);
RollingRateTracker rate;
Point16 ratePos;

// forward declarations
TelemetrySubscriber client("Tests/Sin1");

Format rateFormat("###/s");

void setup()
{
   SerialX::begin();
   feather.begin();

   // Connect to WiFi
   WiFiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

   feather.setTextSize(2);
   feather.setCursorY(-feather.charH());
   feather.println("Subscriber", Color::GRAY);

   feather.println("Initializing", Color::HEADING2);
   feather.moveCursorY(4);

   feather.print("WiFi...", Color::LABEL);
   while (WiFiMulti.run() != WL_CONNECTED)
   {
      feather.print(".", Color::LABEL);
   }
   feather.printlnR("OK", Color::VALUE);
   feather.moveCursorY(1);

   feather.print("WebSocket...", Color::LABEL);

   client.setCallbacks(onConnected, onDisconnected, nullptr, onError, onStarted);
   client.beginSSL(HOST, PORT);
}

void onConnected()
{
   Serial.println("Connected");
}

void onDisconnected()
{
   Serial.println("Disconnected");
   delay(1000); // time for Serial to print
   Util::reset();
}

void onError(std::string msg)
{
   feather.setTextSize(2);
   feather.clearDisplay();
   feather.display.setTextWrap(true);
   feather.println(msg, Color::RED);
   Util::reset(10);
}

void onStarted()
{
   // print the last ok for the WebSocket
   feather.printlnR("OK", Color::VALUE);
   delay(1000); // so the user can see the message

   // display the GUI
   feather.clearDisplay();
   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Subscriber", Color::HEADING);
   feather.moveCursorY(4);

   feather.setTextSize(2);
   feather.print("Topic: ", Color::LABEL);
   feather.println(client.getTopic(), Color::VALUE);
   feather.moveCursorY(1);

   feather.print(" Rate: ", Color::LABEL);
   ratePos = feather.getCursor();
   feather.println("---", Color::VALUE);
   feather.moveCursorY(1);

   Url url(client.getUrl().c_str());
   feather.print(" Host: ", Color::LABEL);
   feather.display.setTextWrap(true);
   feather.println(url.getHost(), Color::VALUE2);
   feather.moveCursorY(1);

   feather.print(" Port: ", Color::LABEL);
   feather.println(url.getPort(), Color::VALUE2);
   feather.moveCursorY(1);

   feather.print(" Schm: ", Color::LABEL);
   feather.println(url.getScheme() + "://", Color::VALUE2);
   feather.moveCursorY(1);

   feather.print(" Path: ", Color::LABEL);
   feather.println(url.getPath(), Color::VALUE2);
   feather.moveCursorY(1);

   sw.start();
}


float lastValue = NAN;
void loop()
{
   client.loop(); // Continuously poll for events and maintain connection

   if (std::isnan(client.getValue()) == false && client.getValue() != lastValue)
   {
      lastValue = client.getValue();
      rate.tick();
   }

   if (sw.elapsedMillis() > 1000)
   {
      feather.setCursor(ratePos);
      feather.setTextSize(2);
      feather.println(rate.getRate(), rateFormat, Color::VALUE);
      sw.reset();
   }
}
