//
// Gate Opener
//
// Hosts a small web server exposing a single "Gate" resource that can be read (GET)
// or updated (POST) as "OPEN" or "CLOSED":
//
// - GET  /           displays a button that triggers the gate to open.
// - GET  /Gate       returns the current gate value ("OPEN" or "CLOSED").
// - POST /Gate       sets the gate value; body must be "OPEN" or "CLOSED".
//
// Hardware: Waveshare ESP32-S3-Zero with a custom-powered I2C bus and RGB LED status
// indicator. The onboard NeoPixel/RGB status LED reflects connection status only
// (white while starting up, blue while connecting to WiFi, green once ready, red on
// failure); the general-purpose LED lights up while the gate is OPEN.
//
// The device restarts automatically at midnight and checks for a firmware update
// periodically.
//

#include <Arduino.h>
#include <WebServer.h>

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"

#include "SerialX.h"
#include "Status.h"
#include "Timer.h"
#include "Util.h"
#include "WiFiSettings.h"

constexpr auto VERSION = 
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Gate_Opener";

constexpr uint16_t WEB_SERVER_PORT = 80;
constexpr uint8_t GATE_RELAY_PIN = 11; // pulsed HIGH to trigger the gate opener
constexpr float GATE_RELAY_PULSE_SECS = 1.0f;
constexpr float WIFI_FAILED_RESET_DELAY_S = 10.0f; // standard error-signal time before resetting

Arduino arduino;
WebServer server(WEB_SERVER_PORT);

bool gateOpen = false;
bool gateRelayPulsing = false;
TimerSecs gateRelayPulseTimer(GATE_RELAY_PULSE_SECS);

///
/// <summary>
/// Updates the general-purpose LED to reflect the current gate state: on while OPEN,
/// off while CLOSED. The combined RGB/NeoPixel status indicator is left alone here,
/// since it only reflects connection status (see arduino.setStatus()).
/// </summary>
///
void updateGateStatus()
{
	if (gateOpen)
	{
	  arduino.led.turnOn();
	}
	else
	{
	  arduino.led.turnOff();
	}
}

///
/// <summary>
/// Starts a GATE_RELAY_PULSE_SECS-long HIGH pulse on the gate relay pin to trigger the
/// gate opener. Call checkGateRelayPulse() every loop() to end the pulse on time.
/// </summary>
///
void startGateRelayPulse()
{
	Serial.println("Gate Signal On");
	digitalWrite(GATE_RELAY_PIN, HIGH);
	gateRelayPulseTimer.reset();
	gateRelayPulsing = true;
}

///
/// <summary>
/// Ends the gate relay pulse once GATE_RELAY_PULSE_SECS has elapsed since it started.
/// </summary>
///
void checkGateRelayPulse()
{
	if (gateRelayPulsing && gateRelayPulseTimer.ready())
	{
		digitalWrite(GATE_RELAY_PIN, LOW);
		gateRelayPulsing = false;
		Serial.println("Gate Signal Off");

		gateOpen = false;
		updateGateStatus();
	}
}

///
/// <summary>
/// Handles GET / by rendering a page with a title, usage instructions, and a button
/// that triggers the gate to open.
/// </summary>
///
void handleRoot()
{
   String html = "<html><body>";
   html += "<h1>Gate Opener</h1>";
   html += "<p>POST \"OPEN\" to http://";
   html += WiFi.localIP().toString();
   html += "/Gate to open the gate.</p>";
   html += "<form method='POST' action='/Gate'>";
   html += "<input type='hidden' name='plain' value='OPEN'>";
   html += "<input type='hidden' name='redirect' value='1'>";
   html += "<button type='submit'>Open Gate</button>";
   html += "</form></body></html>";

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
	  startGateRelayPulse();
	}
	else if (value.equalsIgnoreCase("CLOSED"))
	{
	  gateOpen = false;
	  Serial.println("Gate: CLOSED");
	}
	else
	{
	  Serial.print("Gate: invalid value \"");
	  Serial.print(value);
	  Serial.println("\"");
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

   pinMode(GATE_RELAY_PIN, OUTPUT);

   if (!arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &arduino))
   {
      arduino.setStatus(Status::FAILED);
      Util::setHaltReason("WiFi connect failed");
      Util::reset(WIFI_FAILED_RESET_DELAY_S);
   }

   arduino.enableRebooter();
   arduino.enableOTA(VERSION, SKETCH_NAME);

   server.on("/", HTTP_GET, handleRoot);
   server.on("/Gate", HTTP_GET, handleGetGate);
   server.on("/Gate", HTTP_POST, handlePostGate);
   server.begin();

   Serial.print("Web Server: http://");
   Serial.println(WiFi.localIP());

   arduino.setStatus(Status::READY);
   updateGateStatus();
}

void loop()
{
   server.handleClient();
   arduino.checkForOTA();
   checkGateRelayPulse();
}
