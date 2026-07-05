
// undefine to use the remote server
#define TELEMETRY_LOCAL

#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <WebSocketsClient.h>
#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "SerialX.h"
#include "WiFiSettings.h"
#include "Stopwatch.h"
#include "RollingRate.h"
#include "TelemetryClient.h"
#include "Url.h"
#include "WindMeter.h"

Arduino arduino;

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
   arduino.begin();
   wind.begin();

   // Connect to WiFi
   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

   arduino.setTextSize(2);
   arduino.setCursorY(-arduino.charH());
   arduino.println("Wind Publisher", Color::GRAY);

   arduino.setCursor(0, 0);
   arduino.println("Initializing", Color::HEADING2);
   arduino.moveCursorY(4);

   arduino.print("WiFi...", Color::LABEL);
   while (WiFi.status() != WL_CONNECTED)
   {
      arduino.print(".", Color::LABEL);
   }
   arduino.printlnR("OK", Color::VALUE);
   arduino.moveCursorY(1);

   arduino.print("WebSocket...", Color::LABEL);

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
   arduino.setTextSize(2);
   arduino.clearDisplay();
   arduino.display.setTextWrap(true);
   arduino.println(msg, Color::RED);
   Util::reset(10);
}

void onStarted()
{
   // print the last ok for the WebSocket
   arduino.printlnR("OK", Color::VALUE);
   delay(1000); // so the user can see the message

   // display the GUI
   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(2);
   arduino.println("Wind Publisher", Color::HEADING);
   arduino.moveCursorY(8);

   arduino.setTextSize(2);
   arduino.println("Topic: ", client.getTopic());
   arduino.moveCursorY(1);

   arduino.print(" Rate: ", "---");
   ratePos = arduino.getCursor();
   arduino.println();
   arduino.moveCursorY(1);

   arduino.print("Speed: ", "---");
   speedPos = arduino.getCursor();
   arduino.println();
   arduino.moveCursorY(1);

   Url url(client.getUrl().c_str());
   arduino.display.setTextWrap(true);
   arduino.println(" Host: ", url.getHost(), Color::VALUE2);
   arduino.moveCursorY(1);

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
      arduino.setCursor(ratePos);
      arduino.println(rate.get(), rateFormat, Color::VALUE);
      sw.reset();
   }

   arduino.setCursor(speedPos);
   arduino.println(speed, speedFormat, Color::VALUE);

}
