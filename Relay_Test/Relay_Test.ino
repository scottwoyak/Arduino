//
// Relay Test
//
// Based on Gate_Opener without the web server or InfluxDB uploads. Tests a relay module
// connected to the Waveshare (Sensors) or Waveshare (Sensors2) board by exposing
// DeviceServer commands to pulse, hold on, or turn off the relay on RELAY_PIN (GPIO 13),
// automatically pulsing the relay once an hour on the hour, and monitoring
// RELAY_DETECTION_PIN (GPIO 10).
//
// Hardware: Waveshare ESP32-S3-Zero (Sensors or Sensors2) with a custom-powered I2C bus.
// The status LED (3-pin RGB LED on Sensors, first NeoPixel on Sensors2) reflects
// connection status during startup (white while starting up, blue while connecting to
// WiFi, green while connecting to services) and stays green once ready (Status::RUNNING);
// while the relay is ON, the status LED turns magenta, then returns to the current status
// setting when the relay turns OFF. The second LED (general-purpose LED on Sensors, second
// NeoPixel on Sensors2) lights up while RELAY_DETECTION_PIN is HIGH (in orange on Sensors2).
//
// Logs a message to the LogServer via Logger each time the relay turns on or off, and
// logs an error alert if RELAY_DETECTION_PIN goes HIGH while the relay was not commanded
// on. The device checks for a firmware update periodically.
//

#include <Arduino.h>
#include <array>
#include <sstream>
#include <string>

#include "ArduinoBoard.h"

#if !defined(ARDUINO_WAVESHARE_ESP32S3_ZERO_SENSORS) && !defined(ARDUINO_WAVESHARE_ESP32S3_ZERO_SENSORS2)
#error "This sketch requires the Waveshare (Sensors) or Waveshare (Sensors2) board."
#include "WrongBoard.h"
#endif

#include "LibraryVersion.h"
#include "TimeSync.h"
#include "Timer.h"

#include "WiFiSettings.h"

#include "DeviceSketch.h"

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Relay_Test";

constexpr uint8_t RELAY_POWER_PIN = 5; // held HIGH to power the relay module
constexpr uint8_t RELAY_DETECTION_PIN = 10; // HIGH when the relay output is detected
constexpr uint8_t RELAY_PIN = 13; // pulsed HIGH to trigger the relay
constexpr float RELAY_TRIGGER_S = 1.0f;

///
/// <summary>
/// Commands registered with DeviceServerClient, listed by "Help" after the built-ins.
/// </summary>
///
constexpr uint8_t NUM_RELAY_COMMANDS = 4;
constexpr std::array<DeviceServerClient::CommandInfo, NUM_RELAY_COMMANDS> RELAY_COMMANDS = {{
   { "GetState", "Reports the current relay state (ON, HOLD, or OFF)", { "gs" } },
   { "On", "Pulses the relay on, unless currently held on" },
   { "Hold", "Holds the relay on until Off is issued" },
   { "Off", "Releases a held-on relay (or clears a pending on pulse)" },
}};

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

DeviceSketch sketch(SKETCH_CONFIG);

bool relayTriggered = false;
bool relayHeldOn = false;
bool relayDetected = false;
bool unexpectedRelayDetected = false;
TimerSecs relayTriggerTimer(RELAY_TRIGGER_S);

///
/// <summary>
/// Updates the status LED to reflect the current relay state: magenta while ON, or
/// restores the current status setting while OFF.
/// </summary>
///
void updateRelayStatus()
{
   if (relayTriggered)
   {
      sketch.arduino.statusLed.setColor(Color::MAGENTA);
      sketch.arduino.statusLed.turnOn();
   }
   else
   {
      sketch.arduino.setStatus(sketch.arduino.currentStatus());
   }
}

///
/// <summary>
/// Polls RELAY_DETECTION_PIN and updates the second LED (sketch.arduino.led) to light up
/// while the detection pin is HIGH and turn off while it is LOW. Logs a loud error message
/// if the relay detection pin goes HIGH while the relay was not triggered by this sketch.
/// </summary>
///
void checkRelayDetection()
{
   bool detected = digitalRead(RELAY_DETECTION_PIN) == HIGH;
   if (detected != relayDetected)
   {
      relayDetected = detected;
      if (relayDetected)
      {
         sketch.arduino.led.turnOn();
         if (!relayTriggered)
         {
            unexpectedRelayDetected = true;
            Logger.log("!!! UNEXPECTED RELAY TRIGGER DETECTED (RELAY_DETECTION_PIN HIGH WHILE RELAY OFF) !!!", LogSeverity::ERROR);
         }
      }
      else
      {
         sketch.arduino.led.turnOff();
         if (unexpectedRelayDetected)
         {
            unexpectedRelayDetected = false;
            Logger.log("Unexpected relay trigger cleared (RELAY_DETECTION_PIN LOW)", LogSeverity::WARN);
         }
      }
   }
}

///
/// <summary>
/// Starts a RELAY_TRIGGER_S-long HIGH pulse on the relay pin to trigger the
/// relay. Call checkRelayTrigger() every loop() to end the pulse on time.
/// </summary>
///
void startRelayTrigger()
{
   digitalWrite(RELAY_PIN, HIGH);
   relayTriggerTimer.reset();
   relayTriggered = true;
   Logger.log("Relay Signal On");
}

///
/// <summary>
/// Ends the relay pulse once RELAY_TRIGGER_S has elapsed.
/// </summary>
///
void checkRelayTrigger()
{
   if (relayTriggered && !relayHeldOn && relayTriggerTimer.ready())
   {
      digitalWrite(RELAY_PIN, LOW);
      relayTriggered = false;
      Logger.log("Relay Signal Off");

      updateRelayStatus();
   }
}

///
/// <summary>
/// Releases a held-on relay by turning the relay off. Does nothing if not held.
/// </summary>
///
void releaseRelayHold()
{
   if (relayHeldOn)
   {
      relayHeldOn = false;
      digitalWrite(RELAY_PIN, LOW);
      relayTriggered = false;
      Logger.log("Relay Signal Off");
      updateRelayStatus();
   }
}

///
/// <summary>
/// Pulses the relay once an hour when the local clock hour changes (on the hour).
/// </summary>
///
void checkHourlyTrigger()
{
   static int lastHour = -1;

   if (!TimeSync::isSynced())
   {
      return;
   }

   time_t now = time(nullptr);
   struct tm timeInfo;
   localtime_r(&now, &timeInfo);

   if (lastHour != -1 && timeInfo.tm_hour != lastHour)
   {
      if (!relayHeldOn)
      {
         Logger.log("Hourly relay trigger");
         startRelayTrigger();
         updateRelayStatus();
      }
   }
   lastHour = timeInfo.tm_hour;
}

///
/// <summary>
/// Handles the "GetState"/"On"/"Hold"/"Off" commands registered via
/// sketch.setCommandHandler(), reporting or driving the relay.
/// </summary>
/// <param name="command">Full command text received from the DeviceServer.</param>
///
static void onRelayCommand(const char* command)
{
   std::istringstream stream(command);
   std::string verb;
   stream >> verb;

   if (strcasecmp(verb.c_str(), "GetState") == 0)
   {
      DeviceServerClient::respond(relayHeldOn ? "HOLD" : (relayTriggered ? "ON" : "OFF"));
   }
   else if (strcasecmp(verb.c_str(), "On") == 0)
   {
      if (!relayHeldOn)
      {
         startRelayTrigger();
         updateRelayStatus();
      }
      Logger.log("Relay command On");
      DeviceServerClient::respond("ON");
   }
   else if (strcasecmp(verb.c_str(), "Hold") == 0)
   {
      relayHeldOn = true;
      if (!relayTriggered)
      {
         startRelayTrigger();
      }
      updateRelayStatus();
      Logger.log("Relay command Hold");
      DeviceServerClient::respond("HOLD");
   }
   else if (strcasecmp(verb.c_str(), "Off") == 0)
   {
      releaseRelayHold();
      Logger.log("Relay command Off");
      DeviceServerClient::respond("OFF");
   }
   else
   {
      DeviceServerClient::respond((std::string("Unhandled command: ") + command).c_str());
   }
}

void setup()
{
   pinMode(RELAY_POWER_PIN, OUTPUT);
   digitalWrite(RELAY_POWER_PIN, HIGH);

   pinMode(RELAY_PIN, OUTPUT);
   digitalWrite(RELAY_PIN, LOW);

   pinMode(RELAY_DETECTION_PIN, INPUT_PULLDOWN);

   sketch.setCommandHandler(onRelayCommand, RELAY_COMMANDS.data(), NUM_RELAY_COMMANDS);
   sketch.begin();

   sketch.arduino.setStatus(Status::RUNNING);
   sketch.arduino.led.turnOff();
   checkRelayDetection();

   sketch.completeInitialization();
}

void loop()
{
   checkHourlyTrigger();
   checkRelayTrigger();
   checkRelayDetection();

   sketch.loop();
}
