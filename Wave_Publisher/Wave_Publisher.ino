#include <WiFi.h>
#include "SerialX.h"
#include "WiFiSettings.h"
#include "TelemetryClient.h"
#include "Url.h"
#include "Status.h"
#include "Stopwatch.h"
#include "Timer.h"

constexpr uint8_t TRIGGER_PIN = 6;
constexpr uint8_t ECHO_PIN = 5;

NeoPixelStatus status;

constexpr auto NUM_DECIMALS = 1;
//TelemetryPublisher client("Waves/Lake", NUM_DECIMALS);
TelemetryPublisher client("Waves/Test", NUM_DECIMALS);

Stopwatch restartSW;
Timer publishTimer(100); // 10 per sec

void setup()
{
   status.begin();
   status.setStatus(Status::STARTED);
   delay(1000);

   SerialX::begin();
   Serial.println("Wave Publisher");

   pinMode(TRIGGER_PIN, OUTPUT);
   pinMode(ECHO_PIN, INPUT);

	// Connect to WiFi
	status.setStatus(Status::WIFI_CONNECTING);
	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
	Serial.print("WiFi...");
	while (WiFi.status() != WL_CONNECTED)
	{
	 Serial.print(".");
	}
	Serial.println("OK");

   status.setStatus(Status::WEB_CONNECTING);
   client.setCallbacks(onConnected, onDisconnected, onSendText, onReceiveText, onError, onStarted);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);

   setCpuFrequencyMhz(80);
}

void onConnected()
{
   Serial.println("Connected");
}

void onDisconnected()
{
   Serial.println("Disconnected");
   delay(5000); // time for Serial to print and LED to show
   Util::reset();
}

void onError(std::string msg)
{
   Serial.print("Error: ");
   Serial.println(msg.c_str());
   delay(5000); // time for Serial to print and LED to show
   Util::reset();
}

void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
   if (from.empty()) return;
   size_t start_pos = 0;
   while ((start_pos = str.find(from, start_pos)) != std::string::npos)
   {
	  str.replace(start_pos, from.length(), to);
	  start_pos += to.length(); // Move past the new replacement
   }
}

void onSendText(std::string msg)
{
   Serial.print(">>> ");
   replaceAll(msg, "\n", "\\n");
   msg = '"' + msg + '"';
   Serial.println(msg.c_str());
}

void onReceiveText(std::string msg)
{
   Serial.print("<<< ");
   replaceAll(msg, "\n", "\\n");
   msg = '"' + msg + '"';
   Serial.println(msg.c_str());
}

void onStarted()
{
   status.setStatus(Status::READY);
}

float measure()
{
   digitalWrite(TRIGGER_PIN, LOW);
   delayMicroseconds(2);

   digitalWrite(TRIGGER_PIN, HIGH);
   delayMicroseconds(10);
   digitalWrite(TRIGGER_PIN, LOW);

   long durationMicros = pulseIn(ECHO_PIN, HIGH);
   float distanceCM = durationMicros * 0.034 / 2;
   float distanceIN = distanceCM / 2.54;
   Serial.println("Distance: " + String(distanceCM) + " cm   " + String(distanceIN) + " in   ");

   // avoid echos - less than 100 to allow for 10/s 
   delay(80);

   return distanceCM;
}

void loop()
{
   // restart every 24 hours to play it safe
   if (restartSW.elapsedSecs() > 24 * 60 * 60)
   {
	  Util::reset();
   }

   if (client.isStarted() && publishTimer.ready())
   {
	  float distanceCM = measure();
	  client.setValue(distanceCM);
   }

   client.loop(); // Continuously poll for events and maintain connection
}
