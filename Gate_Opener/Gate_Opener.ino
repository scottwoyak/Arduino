//
// Gate Opener
//
// Hosts a small web server exposing a single "Gate" resource that can be read (GET)
// or triggered to open (POST):
//
// - GET  /           displays a full-window "Open Gate" button.
// - GET  /Gate       returns the current gate value ("OPEN" or "CLOSED").
// - POST /Gate       triggers the gate to open; body must be "OPEN".
//
// Hardware: Waveshare ESP32-S3-Zero with a custom-powered I2C bus and RGB LED status
// indicator. The onboard NeoPixel/RGB status LED reflects connection status only
// (white while starting up, blue while connecting to WiFi, green once ready, red on
// failure); the general-purpose LED lights up while the gate is OPEN. The relay module
// is powered directly (GND/VCC) and triggered via GATE_RELAY_PIN.
//
// Also uploads rolling-averaged enclosure temperature/humidity and a point-in-time
// CPU temperature reading to InfluxDB on a fixed interval (Measurement: Sensors,
// site=Bragg, location=Gate, sensor="Gate Opener", item=<Enclosure|CPU>). Startup/init
// text is also logged to InfluxDB (Measurement: Log) via InfluxLogger.
//
// The device restarts automatically at midnight and checks for a firmware update
// periodically.
//

#include <Arduino.h>
#include <string>
#include <WebServer.h>

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "WiFiSettings.h"

#include "Monitor.h"

constexpr auto VERSION = 
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Gate_Opener";

constexpr uint16_t WEB_SERVER_PORT = 80;
constexpr uint8_t GATE_RELAY_PIN = 13; // pulsed HIGH to trigger the gate opener
constexpr float GATE_RELAY_TRIGGER_SECS = 1.0f;

Arduino arduino;
WebServer server(WEB_SERVER_PORT);

bool gateTriggerRelay = false;
TimerSecs gateRelayTriggerTimer(GATE_RELAY_TRIGGER_SECS);

MonitorConfig MONITOR_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .fixedSite = { nullptr, INFLUXDB_BUCKET, "Bragg", "Gate" },
   .influxSensor = "Gate Opener",
   .includeEnclosureTemp = true,
   .includeCpuTemp = true,
   .enableOTA = true,
   .enableRebooter = true,
};

Monitor monitor(&arduino, MONITOR_CONFIG);

///
/// <summary>
/// Updates the general-purpose LED to reflect the current gate state: on while OPEN,
/// off while CLOSED. The combined RGB/NeoPixel status indicator is left alone here,
/// since it only reflects connection status (see arduino.setStatus()).
/// </summary>
///
void updateGateStatus()
{
	if (gateTriggerRelay)
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
/// Starts a GATE_RELAY_TRIGGER_SECS-long HIGH pulse on the gate relay pin to trigger the
/// gate opener. Call checkGateRelayTrigger() every loop() to end the pulse on time.
/// </summary>
///
void startGateRelayTrigger()
{
	Serial.println("Gate Signal On");
	digitalWrite(GATE_RELAY_PIN, HIGH);
	gateRelayTriggerTimer.reset();
	gateTriggerRelay = true;
}

///
/// <summary>
/// Ends the gate relay pulse once GATE_RELAY_TRIGGER_SECS has elapsed since it started.
/// </summary>
///
void checkGateRelayTrigger()
{
	if (gateTriggerRelay && gateRelayTriggerTimer.ready())
	{
		digitalWrite(GATE_RELAY_PIN, LOW);
		gateTriggerRelay = false;
		Serial.println("Gate Signal Off");

		updateGateStatus();
	}
}

///
/// <summary>
/// Handles GET / by rendering a single full-window button that triggers the gate to
/// open.
/// </summary>
///
void handleRoot()
{
   String html = "<html><body style='margin:0'>";
   html += "<form method='POST' action='/Gate' style='height:100vh;box-sizing:border-box;padding:5vmin;display:flex'>";
   html += "<input type='hidden' name='plain' value='OPEN'>";
   html += "<input type='hidden' name='redirect' value='1'>";
   html += "<button type='submit' style='flex:1;font-size:15vmin'>Open Gate</button>";
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
   server.send(200, "text/plain", gateTriggerRelay ? "OPEN" : "CLOSED");
}

///
/// <summary>
/// Handles POST /Gate by parsing the request body (must be "OPEN") and triggering the
/// gate relay pulse. Responds with 400 for any other value.
/// </summary>
///
void handlePostGate()
{
	bool redirect = server.hasArg("redirect");
	String value = server.arg("plain");
	value.trim();

	if (value.equalsIgnoreCase("OPEN"))
	{
	  startGateRelayTrigger();
	  updateGateStatus();
	}
	else
	{
	  Serial.print("Gate: invalid value \"");
	  Serial.print(value);
	  Serial.println("\"");
	  server.send(400, "text/plain", "Value must be OPEN");
	  return;
	}

	if (redirect)
	{
		server.sendHeader("Location", "/");
		server.send(303);
	}
	else
	{
		server.send(200, "text/plain", "OPEN");
	}
}

void setup()
{
   pinMode(GATE_RELAY_PIN, OUTPUT);
   digitalWrite(GATE_RELAY_PIN, LOW);

   monitor.begin();

   server.on("/", HTTP_GET, handleRoot);
   server.on("/Gate", HTTP_GET, handleGetGate);
   server.on("/Gate", HTTP_POST, handlePostGate);
   server.begin();

   std::string webServerMessage = std::string("Web Server: http://") + WiFi.localIP().toString().c_str();
   monitor.logMessage(webServerMessage.c_str());

   arduino.setStatus(Status::READY);
   updateGateStatus();
}

void loop()
{
   server.handleClient();
   checkGateRelayTrigger();

   monitor.loop();
}

