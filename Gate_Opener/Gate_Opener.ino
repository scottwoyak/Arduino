//
// Gate Opener
//
// Hosts a small web server exposing a single "Gate" resource that can be read (GET)
// or updated (POST) as "OPEN" or "CLOSED":
//
// - GET  /           displays the current gate state and a button to toggle it.
// - GET  /Gate       returns the current gate value ("OPEN" or "CLOSED").
// - POST /Gate       sets the gate value; body must be "OPEN" or "CLOSED".
//
// Hardware: Waveshare ESP32-S3-Zero, using the onboard NeoPixel LED for status:
// white while starting up, blue while connecting to WiFi, green once ready (gate
// CLOSED), and orange while the gate is OPEN.
//
// The device restarts automatically at midnight and checks for a firmware update
// every OTA_CHECK_INTERVAL_M minutes.
//

#include <Arduino.h>
#include <WebServer.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_NEOPIXEL_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Waveshare ESP32-S3-Zero)."
#endif

#include "SerialX.h"
#include "Status.h"
#include "WiFiSettings.h"

constexpr auto VERSION = 
#include "version.txt"
;
constexpr auto OTA_FIRMWARE_URL = "https://github.com/scottwoyak/Arduino/releases/download/Gate-Opener/Gate_Opener.ino.bin";
constexpr uint8_t OTA_CHECK_INTERVAL_M = 10;

constexpr uint16_t WEB_SERVER_PORT = 80;

Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);
WebServer server(WEB_SERVER_PORT);

bool gateOpen = false;

///
/// <summary>
/// Updates the NeoPixel to reflect the current gate state: orange while OPEN,
/// green (READY) while CLOSED.
/// </summary>
///
void updateGateStatus()
{
   if (gateOpen)
   {
	  status.setStatus(1.0f, 0.5f, 0.0f);
   }
   else
   {
	  status.setStatus(Status::READY);
   }
}

///
/// <summary>
/// Handles GET / by rendering a page showing the current gate state and a button
/// that toggles it.
/// </summary>
///
void handleRoot()
{
   const char* label = gateOpen ? "Close Gate" : "Open Gate";
   const char* nextValue = gateOpen ? "CLOSED" : "OPEN";

   String html = "<html><body>";
   html += "<h1>Gate: ";
   html += gateOpen ? "OPEN" : "CLOSED";
   html += "</h1>";
   html += "<form method='POST' action='/Gate'>";
   html += "<input type='hidden' name='plain' value='";
   html += nextValue;
   html += "'>";
   html += "<input type='hidden' name='redirect' value='1'>";
   html += "<button type='submit'>";
   html += label;
   html += "</button></form></body></html>";

   server.send(200, "text/html", html);
}

///
/// <summary>
/// Handles GET /Gate by returning the current gate value as plain text.
/// </summary>
///
void handleGetGate()
{
   server.send(200, "text/plain", gateOpen ? "OPEN" : "CLOSED");
}

///
/// <summary>
/// Handles POST /Gate by parsing the request body ("OPEN" or "CLOSED") and updating
/// the gate value and status LED accordingly. Responds with 400 for any other value.
/// </summary>
///
void handlePostGate()
{
   bool redirect = server.hasArg("redirect");
   String value = server.arg("plain");
   value.trim();

   if (value.equalsIgnoreCase("OPEN"))
   {
	  gateOpen = true;
   }
   else if (value.equalsIgnoreCase("CLOSED"))
   {
	  gateOpen = false;
   }
   else
   {
	  server.send(400, "text/plain", "Value must be OPEN or CLOSED");
	  return;
   }

   updateGateStatus();

   if (redirect)
   {
      server.sendHeader("Location", "/");
      server.send(303);
   }
   else
   {
      server.send(200, "text/plain", gateOpen ? "OPEN" : "CLOSED");
   }
}

void setup()
{
   SerialX::begin();
   Serial.println("Gate Opener");

   arduino.begin();
   status.begin();

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &status);
   arduino.enableRebooter();
   arduino.enableOTA(VERSION, OTA_FIRMWARE_URL, OTA_CHECK_INTERVAL_M * 60.0f);

   server.on("/", HTTP_GET, handleRoot);
   server.on("/Gate", HTTP_GET, handleGetGate);
   server.on("/Gate", HTTP_POST, handlePostGate);
   server.begin();

   Serial.print("Web server: http://");
   Serial.print(WiFi.localIP());

   updateGateStatus();
}

void loop()
{
   server.handleClient();
   arduino.loop();
}
