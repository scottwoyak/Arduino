//
// Mouse Trap
//
// Monitors a VL53L1X time-of-flight distance sensor over I2C and drives a trap-door
// servo on GPIO 5. Starts with the trap door open (0 degrees) and armed. When an object
// is detected within TRIGGER_DISTANCE_MM (50 mm) while the trap is open, the servo moves
// to the closed position (90 degrees), logs a trigger message to the LogServer, and
// ignores further detections until reopened via command or button.
// Pressing the external button (GPIO 9) or the onboard button (buttonA, GPIO 0) toggles
// the trap between open and closed.
//
// Displays the current trap state ("OPEN" or "CLOSED") and live distance in millimeters
// on the Feather's TFT display, and drives a 3-LED WS2812 NeoPixel strip on GPIO 6:
// - Pixel 0 (statusLed): Startup and network connection status.
// - Pixel 1 (led2):      Lights up while an object is within TRIGGER_DISTANCE_MM.
// - Pixel 2 (led3):      Lights up magenta while the trap door is closed.
//
// Exposes DeviceServer commands:
// - GetState (gs):    Reports the current trap state ("OPEN" or "CLOSED").
// - GetReadings (gr): Reports the trap state and latest distance reading in mm.
// - Open:             Moves the servo to 0 degrees and re-arms the distance trigger.
// - Close:            Moves the servo to 90 degrees.
//
// Hardware: Adafruit Feather ESP32-S3 TFT with a VL53L1X distance sensor on I2C, a servo
// on GPIO 5, a 3-LED NeoPixel strip on GPIO 6, and a push button on GPIO 9.
//

#include <Arduino.h>
#include <Wire.h>
#include <array>
#include <sstream>
#include <string>

#include <Adafruit_VL53L1X.h>

#define TEXT_SIZE_4
#include "ArduinoBoard.h"

#if !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
#error "This sketch requires the Adafruit Feather ESP32-S3 TFT board."
#include "WrongBoard.h"
#endif

#include "Format.h"
#include "LibraryVersion.h"

#include "WiFiSettings.h"

#include "DeviceSketch.h"

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Mouse_Trap";

constexpr uint8_t SERVO_PIN = 5;
constexpr uint8_t NEOPIXEL_STRIP_PIN = 6;
constexpr uint8_t BUTTON_PIN = 9;
constexpr uint8_t VL53L1X_I2C_ADDRESS = 0x29;
constexpr int16_t TRIGGER_DISTANCE_MM = 50;

constexpr uint16_t NUM_STRIP_LEDS = 3;
constexpr uint16_t STATUS_PIXEL_INDEX = 0;
constexpr uint16_t LED2_PIXEL_INDEX = 1;
constexpr uint16_t LED3_PIXEL_INDEX = 2;
constexpr uint8_t NEOPIXEL_BRIGHTNESS = 1;

constexpr uint8_t SERVO_OPEN_DEGREES = 0;
constexpr uint8_t SERVO_CLOSED_DEGREES = 90;
constexpr uint8_t SERVO_MAX_DEGREES = 180;

constexpr uint32_t SERVO_PWM_FREQ_HZ = 50;
constexpr uint8_t SERVO_PWM_RESOLUTION_BITS = 14;
constexpr uint32_t SERVO_PERIOD_MICROS = 20000;
constexpr uint32_t SERVO_MIN_PULSE_MICROS = 500;
constexpr uint32_t SERVO_MAX_PULSE_MICROS = 2500;

constexpr uint8_t HEADER_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t VALUE_SIZE = 4;
constexpr uint8_t FOOTER_SIZE = DEFAULT_FOOTER_SIZE;

///
/// <summary>
/// Commands registered with DeviceServerClient, listed by "Help" after the built-ins.
/// </summary>
///
constexpr uint8_t NUM_TRAP_COMMANDS = 3;
constexpr std::array<DeviceServerClient::CommandInfo, NUM_TRAP_COMMANDS> TRAP_COMMANDS = {{
   { "GetState", "Reports the current trap state (OPEN or CLOSED)", { "gs" } },
   { "Open", "Opens the trap door (0 degrees) and arms the distance trigger" },
   { "Close", "Closes the trap door (90 degrees)" },
}};

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

DeviceSketch sketch(SKETCH_CONFIG);
Adafruit_VL53L1X distanceSensor;
Button trapButton(BUTTON_PIN);

NeoPixelStrip strip(NUM_STRIP_LEDS, NEOPIXEL_STRIP_PIN);
NeoPixelLED statusLed(&strip, STATUS_PIXEL_INDEX);
NeoPixelStatus stripStatus(&statusLed);
NeoPixelLED led2(&strip, LED2_PIXEL_INDEX);
NeoPixelLED led3(&strip, LED3_PIXEL_INDEX);

Format distFormat("#### mm", Format::Alignment::CENTER);

bool trapClosed = false;
bool targetDetected = false;
int16_t distanceMm = -1;
int16_t valueStartY = 0;
bool displayReady = false;

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
/// Renders the current trap state and latest distance measurement in the center of the display.
/// </summary>
///
void updateDisplay()
{
   if (!displayReady)
   {
      return;
   }

   bool validDistance = distanceMm >= 0;

   sketch.arduino.setTextSize(VALUE_SIZE);
   sketch.arduino.setCursorY(valueStartY);
   sketch.arduino.printlnC(
      trapClosed ? "CLOSED" : " OPEN ",
      trapClosed ? Color::RED : Color::GREEN);
   sketch.arduino.printlnC(
      validDistance ? static_cast<float>(distanceMm) : 0.0f,
      distFormat,
      validDistance ? Color::VALUE : Color::GRAY);
}

///
/// <summary>
/// Updates led3 (pixel 2 of the NeoPixel strip) and the display to reflect the current
/// trap door state: magenta while CLOSED, off while OPEN.
/// </summary>
///
void updateTrapStatus()
{
   if (trapClosed)
   {
      led3.turnOn();
   }
   else
   {
      led3.turnOff();
   }

   updateDisplay();
}

///
/// <summary>
/// Opens the trap door (moves the servo to SERVO_OPEN_DEGREES) and arms the distance trigger.
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
/// Polls the external button (GPIO 9) and onboard button (buttonA, GPIO 0) and toggles
/// the trap between open and closed when either is pressed.
/// </summary>
///
void checkButton()
{
   if (trapButton.wasPressed() || sketch.arduino.buttonA.wasPressed())
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
/// Polls the VL53L1X distance sensor, updates led2 (pixel 1 of the NeoPixel strip) while
/// an object is within TRIGGER_DISTANCE_MM, updates the display, and closes the trap
/// (logging a trigger message) when an object enters range while the trap is open.
/// </summary>
///
void checkDistanceSensor()
{
   if (!distanceSensor.dataReady())
   {
      return;
   }

   distanceMm = distanceSensor.distance();
   distanceSensor.clearInterrupt();

   bool detected = distanceMm >= 0 && distanceMm <= TRIGGER_DISTANCE_MM;
   if (detected != targetDetected)
   {
      targetDetected = detected;
      if (targetDetected)
      {
         led2.turnOn();
         if (!trapClosed)
         {
            closeTrap();
            Logger.log("Mouse trap triggered at " + std::to_string(distanceMm) + " mm");
         }
      }
      else
      {
         led2.turnOff();
      }
   }

   updateDisplay();
}

///
/// <summary>
/// Formats the current trap state and distance reading for the "GetReadings" command.
/// </summary>
/// <returns>Formatted readings text.</returns>
///
static std::string getReadings()
{
   std::ostringstream text;
   text << "state=" << (trapClosed ? "CLOSED" : "OPEN") << "\n"
        << "distance=" << distanceMm << " mm";
   return text.str();
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
   ledcAttach(SERVO_PIN, SERVO_PWM_FREQ_HZ, SERVO_PWM_RESOLUTION_BITS);
   setServoDegrees(SERVO_OPEN_DEGREES);

   trapButton.begin();

   strip.begin();
   statusLed.begin();
   statusLed.setLevel(NEOPIXEL_BRIGHTNESS);
   sketch.arduino.status.addStatus(&stripStatus);
   led2.begin();
   led2.setLevel(NEOPIXEL_BRIGHTNESS);
   led2.turnOff();
   led3.begin();
   led3.setLevel(NEOPIXEL_BRIGHTNESS);
   led3.setColor(Color::MAGENTA);
   led3.turnOff();

   // Adafruit_VL53L1X::begin() retries its I2C reads forever if the sensor doesn't respond,
   // so verify the sensor acknowledges its I2C address first.
   sketch.addSensor("VL53L1X", []() {
      Wire.beginTransmission(VL53L1X_I2C_ADDRESS);
      return Wire.endTransmission() == 0 && distanceSensor.begin() && distanceSensor.startRanging();
   });
   sketch.onReadings(getReadings, "Reports the trap state and latest distance reading");
   sketch.setCommandHandler(onTrapCommand, TRAP_COMMANDS.data(), NUM_TRAP_COMMANDS);
   sketch.begin();

   sketch.arduino.setStatus(Status::RUNNING);
   sketch.completeInitialization();

   // Draw static heading and footer after completeInitialization() clears the startup screen
   sketch.arduino.setTextSize(HEADER_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Mouse Trap", Color::HEADING);
   int16_t headingHeight = sketch.arduino.charH();
   int16_t footerHeight = sketch.arduino.charH(FOOTER_SIZE);
   int16_t valuesHeight = 2 * sketch.arduino.charH(VALUE_SIZE);
   int16_t availableHeight = sketch.arduino.height() - headingHeight - footerHeight;
   valueStartY = headingHeight + (availableHeight - valuesHeight) / 2;

   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(0, sketch.arduino.height() - footerHeight);
   sketch.arduino.printR(VERSION, Color::LIGHTGRAY);

   displayReady = true;
   updateTrapStatus();
}

void loop()
{
   checkButton();
   checkDistanceSensor();

   sketch.loop();
}
