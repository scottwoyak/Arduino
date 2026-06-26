
// undefine to use the remote server
#define TELEMETRY_LOCAL

#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <WebSocketsClient.h>
#include "Feather.h"
#include "SerialX.h"
#include "WiFiSettings.h"
#include "Stopwatch.h"
#include "RollingRate.h"
#include "TelemetryClient.h"
#include "Url.h"
#include "WindMeter.h"

Feather feather;

Stopwatch sw(false);
RollingRate rate(10);
Point16 ratePos;
Point16 speedPos;

Format rateFormat("###/s");
Format speedFormat("##.# mph");

TelemetryPublisher client("Wind/Studio", 3);
WindMeter wind(A5);

float getValue()
{
   constexpr ulong period = 2 * 1000 * 1000;
   float x = 2 * std::numbers::pi * (((float)(micros() % period)) / period);
   return sin(x);
}

void setup()
{
   SerialX::begin();
   feather.begin();
   wind.begin();

   // Connect to WiFi
   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

   feather.setTextSize(2);
   feather.setCursorY(-feather.charH());
   feather.println("Wind Publisher", Color::GRAY);

   feather.setCursor(0, 0);
   feather.println("Initializing", Color::HEADING2);
   feather.moveCursorY(4);

   feather.print("WiFi...", Color::LABEL);
   while (WiFi.status() != WL_CONNECTED)
   {
      feather.print(".", Color::LABEL);
   }
   feather.printlnR("OK", Color::VALUE);
   feather.moveCursorY(1);

   feather.print("WebSocket...", Color::LABEL);

   client.setCallbacks(onConnected, onDisconnected, nullptr, onReceivedText, onError, onStarted);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);
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

void onReceivedText(std::string payload)
{
   rate.tick();
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
   feather.setTextSize(2);
   feather.println("Wind Publisher", Color::HEADING);
   feather.moveCursorY(8);

   feather.setTextSize(2);
   feather.println("Topic: ", client.getTopic());
   feather.moveCursorY(1);

   feather.print(" Rate: ", "---");
   ratePos = feather.getCursor();
   feather.println();
   feather.moveCursorY(1);

   feather.print("Speed: ", "---");
   speedPos = feather.getCursor();
   feather.println();
   feather.moveCursorY(1);

   Url url(client.getUrl().c_str());
   feather.display.setTextWrap(true);
   feather.println(" Host: ", url.getHost(), Color::VALUE2);
   feather.moveCursorY(1);

   rate.reset();
   sw.start();
}

void loop()
{
   float speed = wind.getSpeed();
   client.setValue(speed);

   client.loop(); // Continuously poll for events and maintain connection

   if (client.isStarted() && sw.elapsedMillis() > 1000)
   {
      feather.setCursor(ratePos);
      feather.println(rate.get(), rateFormat, Color::VALUE);
      sw.reset();
   }

   feather.setCursor(speedPos);
   feather.println(speed, speedFormat, Color::VALUE);

}
