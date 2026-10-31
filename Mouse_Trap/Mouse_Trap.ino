//
// Mouse Trap
//
// Monitors an SR602 PIR motion sensor on GPIO 8 and drives a trap-door servo on GPIO 7.
// Starts with the trap door open (0 degrees) and armed. When motion is detected while the
// trap is open, the servo moves to the closed position (90 degrees), logs a trigger
// message to the LogServer, and ignores further motion until reopened via command or button.
// Pressing the button on GPIO 9 toggles the trap between open and closed.
//
// Exposes DeviceServer commands:
// - GetState (gs): Reports the current trap state ("OPEN" or "CLOSED").
// - Open:          Moves the servo to 0 degrees and re-arms the motion sensor.
// - Close:         Moves the servo to 90 degrees.
//
// Hardware: Waveshare ESP32-S3-Zero (Sensors2) with a 3-LED WS2812 NeoPixel strip on
// GPIO 6. Pixel 0 (mirrored on the onboard NeoPixel) reflects connection status, pixel 1
// (led2) lights up while the trap is closed, and pixel 2 (led3) lights up while the SR602
// motion sensor output is HIGH.
//

#include <Arduino.h>
#include <array>
#include <sstream>
#include <string>

#include "ArduinoBoard.h"

#if !defined(ARDUINO_WAVESHARE_ESP32S3_ZERO_SENSORS2)
#error "This sketch requires the Waveshare (Sensors2) board."
#include "WrongBoard.h"
#endif

#include "LibraryVersion.h"
#include "Button.h"

#include "WiFiSettings.h"

#include "DeviceSketch.h"

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Mouse_Trap";

constexpr uint8_t SERVO_PIN = 7;
constexpr uint8_t MOTION_PIN = 8;
constexpr uint8_t BUTTON_PIN = 9;

constexpr uint8_t SERVO_OPEN_DEGREES = 0;
constexpr uint8_t SERVO_CLOSED_DEGREES = 90;
constexpr uint8_t SERVO_MAX_DEGREES = 180;

constexpr uint32_t SERVO_PWM_FREQ_HZ = 50;
constexpr uint8_t SERVO_PWM_RESOLUTION_BITS = 14;
constexpr uint32_t SERVO_PERIOD_MICROS = 20000;
constexpr uint32_t SERVO_MIN_PULSE_MICROS = 500;
constexpr uint32_t SERVO_MAX_PULSE_MICROS = 2500;

///
/// <summary>
/// Commands registered with DeviceServerClient, listed by "Help" after the built-ins.
/// </summary>
///
constexpr uint8_t NUM_TRAP_COMMANDS = 3;
constexpr std::array<DeviceServerClient::CommandInfo, NUM_TRAP_COMMANDS> TRAP_COMMANDS = {{
   { "GetState", "Reports the current trap state (OPEN or CLOSED)", { "gs" } },
   { "Open", "Opens the trap door (0 degrees) and arms the motion trigger" },
   { "Close", "Closes the trap door (90 degrees)" },
}};

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

DeviceSketch sketch(SKETCH_CONFIG);
Button button(BUTTON_PIN);

bool trapClosed = false;
bool motionDetected = false;

///
/// <summary>
/// Moves the trap-door servo to the specified angle in degrees (0 - 180).
/// </summary>
/// <param name="degrees">Target servo angle in degrees.</param>
///
void setServoDegrees(uint8_t degrees)
{
   uint8_t clampedDegrees = degrees > SERVO_MAX_DEGREES ? SERVO_MAX_DEGREES : degrees;
   uint32_t pulseMicros = SERVO_MIN_PULSE_MICROS +
      ((SERVO_MAX_PULSE_MICROS - SERVO_MIN_PULSE_MICROS) * clampedDegrees) / SERVO_MAX_DEGREES;
   uint32_t duty = (pulseMicros * (1UL << SERVO_PWM_RESOLUTION_BITS)) / SERVO_PERIOD_MICROS;
   ledcWrite(SERVO_PIN, duty);
}

///
/// <summary>
/// Updates led2 on the Sensors2 NeoPixel strip to reflect the trap door state: on while
/// CLOSED, off while OPEN.
/// </summary>
///
void updateTrapStatus()
{
   if (trapClosed)
   {
      sketch.arduino.led2.turnOn();
   }
   else
   {
      sketch.arduino.led2.turnOff();
   }
}

///
/// <summary>
/// Opens the trap door (moves the servo to SERVO_OPEN_DEGREES) and arms the motion trigger.
/// </summary>
///
void openTrap()
{
   trapClosed = false;
   setServoDegrees(SERVO_OPEN_DEGREES);
   updateTrapStatus();
}

///
/// <summary>
/// Closes the trap door (moves the servo to SERVO_CLOSED_DEGREES).
/// </summary>
///
void closeTrap()
{
   trapClosed = true;
   setServoDegrees(SERVO_CLOSED_DEGREES);
   updateTrapStatus();
}

///
/// <summary>
/// Polls the SR602 motion sensor on MOTION_PIN, updates led3 while motion is active, and
/// closes the trap (logging a trigger message) when motion is detected while the trap is open.
/// </summary>
///
void checkMotionSensor()
{
   bool detected = digitalRead(MOTION_PIN) == HIGH;
   if (detected != motionDetected)
   {
      motionDetected = detected;
      if (motionDetected)
      {
         sketch.arduino.led3.turnOn();
         if (!trapClosed)
         {
            closeTrap();
            Logger.log("Mouse trap triggered by motion sensor");
         }
      }
      else
      {
         sketch.arduino.led3.turnOff();
      }
   }
}

///
/// <summary>
/// Polls the button on BUTTON_PIN and toggles the trap between open and closed when pressed.
/// </summary>
///
void checkButton()
{
   if (button.wasPressed())
   {
      if (trapClosed)
      {
         openTrap();
         Logger.log("Mouse trap opened by button");
      }
      else
      {
         closeTrap();
         Logger.log("Mouse trap closed by button");
      }
   }
}

///
/// <summary>
/// Handles the "GetState"/"Open"/"Close" commands registered via sketch.setCommandHandler().
/// </summary>
/// <param name="command">Full command text received from the DeviceServer.</param>
///
static void onTrapCommand(const char* command)
{
   std::istringstream stream(command);
   std::string verb;
   stream >> verb;

   if (strcasecmp(verb.c_str(), "GetState") == 0)
   {
      DeviceServerClient::respond(trapClosed ? "CLOSED" : "OPEN");
   }
   else if (strcasecmp(verb.c_str(), "Open") == 0)
   {
      openTrap();
      Logger.log("Trap command Open");
      DeviceServerClient::respond("OPEN");
   }
   else if (strcasecmp(verb.c_str(), "Close") == 0)
   {
      closeTrap();
      Logger.log("Trap command Close");
      DeviceServerClient::respond("CLOSED");
   }
   else
   {
      DeviceServerClient::respond((std::string("Unhandled command: ") + command).c_str());
   }
}

void setup()
{
   pinMode(MOTION_PIN, INPUT_PULLDOWN);
   button.begin();
   ledcAttach(SERVO_PIN, SERVO_PWM_FREQ_HZ, SERVO_PWM_RESOLUTION_BITS);
   setServoDegrees(SERVO_OPEN_DEGREES);

   sketch.setCommandHandler(onTrapCommand, TRAP_COMMANDS.data(), NUM_TRAP_COMMANDS);
   sketch.begin();

   sketch.arduino.setStatus(Status::RUNNING);
   sketch.arduino.led3.turnOff();
   updateTrapStatus();
   checkMotionSensor();

   sketch.completeInitialization();
}

void loop()
{
   checkButton();
   checkMotionSensor();

   sketch.loop();
}
