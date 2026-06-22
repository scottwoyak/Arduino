
//
// Wind Publisher
//

#include <WiFi.h>
#include <SerialX.h>
#include <WiFiSettings.h>
#include <TelemetryClient.h>
#include <Url.h>
#include <WindMeter.h>
#include <Status.h>

constexpr uint8_t NUM_DECIMALS = 2;
TelemetryPublisher client("Wind/Lake", NUM_DECIMALS);

constexpr uint8_t WIND_PIN = 13;
WindMeter wind(WIND_PIN, 0);

constexpr auto WHITE_LED_PIN = 10;
constexpr auto BLUE_LED_PIN = 9;
constexpr auto GREEN_LED_PIN = 8;
constexpr auto RED_LED_PIN = 7;

LedStatus status(WHITE_LED_PIN, BLUE_LED_PIN, GREEN_LED_PIN);

LED redLed(RED_LED_PIN);

void setup()
{
   status.begin();
   redLed.begin();

   status.setStatus(Status::STARTED);

   SerialX::begin();
   Serial.println("Wind Publisher");

   wind.begin();

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
   client.setCallbacks(onConnected, onDisconnected, onSendText, onReceiveText, onError, nullptr);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);

   setCpuFrequencyMhz(80); // keep things cool  
}

void onConnected()
{
   Serial.println("Connected");
   status.setStatus(Status::READY);
}

void onDisconnected()
{
   Serial.println("Disconnected");
   delay(1000); // time for Serial to print and LED to show
   Util::reset();
}

void onError(std::string msg)
{
   Serial.print("Error: ");
   Serial.println(msg.c_str());
   delay(1000); // time for Serial to print and LED to show
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

void loop()
{
   if (client.isStarted())
   {
      // Waveshare crashes if we set the LED value in the interrupt, so we manually do it
      if (wind.ledState())
      {
         redLed.turnOn();
      }
      else
      {
         redLed.turnOff();
      }

      // without a delay, the waveshare crashes
      delay(1);

      float speed = wind.getSpeed();
      client.setValue(speed);
   }

   client.loop(); // Continuously poll for events and maintain connection
}
