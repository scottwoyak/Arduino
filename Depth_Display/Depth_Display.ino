//
// Displays water depth delta in centimeters relative to a calibrated baseline, using either an
// MS5837 pressure sensor or a capacitor-based depth sensor.
//
// To switch sensors, change the DEPTH_SENSOR_TYPE define below to either DEPTH_SENSOR_MS5837 or
// DEPTH_SENSOR_CAPACITOR.
//
// Initializes the I2C bus, Feather display, and depth sensor, retrying sensor initialization
// until successful and showing a visible error message on display in the meantime. Stores
// startup depth as the initial baseline, then continuously reads depth and displays delta depth
// (currentDepthCm - baselineDepthCm).
//
// Calibration flow (Button A):
// - MS5837: pressing button A starts a calibration prompt asking the user to take the sensor out
//   of the water; pressing button A again captures that reading as the new baseline and prompts
//   the user to fully submerge the sensor; pressing button A a third time captures that reading
//   as the new maximum depth (used to scale the depth bar), saves both values to Preferences, and
//   resumes normal display.
// - Capacitor: pressing button A starts a calibration prompt asking the user to hold the sensor
//   in air; pressing button A again captures the in-air charge time and prompts the user to
//   submerge the sensor exactly 18 inches; pressing button A a third time captures that charge
//   time, calibrates the sensor's charge-time-to-depth mapping from those two points, saves both
//   raw charge times to Preferences, and resumes normal display.
// The saved calibration is automatically restored on startup. If no calibration has been saved
// yet, the calibration sequence starts automatically on startup.
//
// Display output: a heading and sensor-type subheading at the top, the signed depth delta in
// centimeters centered below them, a blue depth level bar on the right (full at zero depth, empty
// at the calibrated maximum depth), and the live sensor read rate in gray in the lower right corner.
// For the capacitor sensor, the rolling-average buffer size is shown in the upper right corner and
// can be adjusted live with Encoder B (requires a Playground board); the value is persisted to
// Preferences and restored on startup.
//

// ----------- Sensor Selection
// Change this define to switch which depth sensor implementation the sketch uses.
#define DEPTH_SENSOR_MS5837 1
#define DEPTH_SENSOR_CAPACITOR 2
#define DEPTH_SENSOR_TYPE DEPTH_SENSOR_CAPACITOR

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include "ArduinoBoard.h"
#include "Bar.h"
#include "DisplayField.h"
#include "RollingRate.h"
#include "SerialX.h"

#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_MS5837
#include <MS5837.h>
#include "MS5837DepthSensor.h"
#elif DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
#include "CapacitorDepthSensor.h"
#else
#error "DEPTH_SENSOR_TYPE must be set to DEPTH_SENSOR_MS5837 or DEPTH_SENSOR_CAPACITOR."
#endif

#ifndef ARDUINO_BUTTON_A_SUPPORTED
#error "This sketch requires a board with a Button A (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
#ifndef ARDUINO_PLAYGROUND_SUPPORTED
#error "The capacitor sensor's buffer size control requires a Playground board (e.g. ESP32S3 Dev Module) with Encoder B."
#endif
#endif

constexpr float FRESH_WATER_DENSITY_KG_PER_M3 = 997.0f;

constexpr uint8_t CAPACITOR_CHARGE_PIN = CapacitorSensor::CHARGE_PIN_100K;
constexpr uint8_t CAPACITOR_SENSE_PIN = CapacitorSensor::SENSE_PIN;
constexpr float CAPACITOR_DEFAULT_ZERO_CHARGE_TIME = 128.3f;
constexpr float CAPACITOR_DEFAULT_CALIBRATION_CHARGE_TIME = 295.0f;
constexpr float CAPACITOR_CALIBRATION_DEPTH_CM = 45.72f; // 18 inches (half of full depth)
constexpr float CAPACITOR_FULL_DEPTH_CM = 91.44f; // 36 inches
constexpr int32_t CAPACITOR_DEFAULT_BUFFER_SIZE = 30; // matches CapacitorSensor::DEFAULT_BUFFER_SIZE
constexpr int32_t CAPACITOR_BUFFER_SIZE_MIN = 1;
constexpr int32_t CAPACITOR_BUFFER_SIZE_MAX = 200;

Arduino arduino;

#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_MS5837
MS5837DepthSensor sensor(MS5837::MS5837_02BA, FRESH_WATER_DENSITY_KG_PER_M3);
constexpr auto SENSOR_TYPE_NAME = "MS5837-02BA";
#elif DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
CapacitorDepthSensor sensor(
   CAPACITOR_CHARGE_PIN,
   CAPACITOR_SENSE_PIN,
   CAPACITOR_DEFAULT_ZERO_CHARGE_TIME,
   CAPACITOR_DEFAULT_CALIBRATION_CHARGE_TIME,
   CAPACITOR_CALIBRATION_DEPTH_CM,
   CAPACITOR_FULL_DEPTH_CM);
constexpr auto SENSOR_TYPE_NAME = "Capacitor";
#endif

Preferences preferences;

constexpr auto PREFERENCES_NAMESPACE = "DepthDisplay";
constexpr auto BASELINE_DEPTH_KEY = "baselineCm";
constexpr auto MAX_DEPTH_KEY = "maxDepthCm";
constexpr auto CAP_ZERO_TIME_KEY = "capZeroTime";
constexpr auto CAP_CALIBRATION_TIME_KEY = "capCalibTime";
constexpr auto CAP_BUFFER_SIZE_KEY = "capBufSize";

Format deltaDepthFormat("####.## cm");
Format rateFormat("###/s");
Format bufferSizeFormat("Buf:###");

constexpr float DEFAULT_FULL_DEPTH_FEET = 3.0f;
constexpr float CENTIMETERS_PER_FOOT = 30.48f;
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_MS5837
constexpr float DEFAULT_MAX_DEPTH_CM = DEFAULT_FULL_DEPTH_FEET * CENTIMETERS_PER_FOOT;
#elif DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
constexpr float DEFAULT_MAX_DEPTH_CM = CAPACITOR_FULL_DEPTH_CM;
#endif
constexpr uint8_t TITLE_TEXT_SIZE = 3;
constexpr uint8_t SUBTITLE_TEXT_SIZE = 2;
constexpr uint8_t DEPTH_TEXT_SIZE = 5;
constexpr uint8_t RATE_TEXT_SIZE = 2;
constexpr uint16_t DEPTH_BAR_WIDTH_PX = 10;

///
/// <summary>
/// Tracks progress through the calibration sequence.
/// </summary>
///
enum class CalibrationState : uint8_t
{
   None,
   WaitFirstPoint,
   WaitSecondPoint
};

float baselineDepthCm = NAN;
float maxDepthCm = DEFAULT_MAX_DEPTH_CM;
float capZeroChargeTime = CAPACITOR_DEFAULT_ZERO_CHARGE_TIME;
float capCalibrationChargeTime = CAPACITOR_DEFAULT_CALIBRATION_CHARGE_TIME;
int32_t capBufferSize = CAPACITOR_DEFAULT_BUFFER_SIZE;
RollingRate readRate;
CalibrationState calibrationState = CalibrationState::None;

DisplayField* depthField = nullptr;
DisplayField* rateField = nullptr;
DisplayField* bufferSizeField = nullptr;
VerticalBar* depthBar = nullptr;

///
/// <summary>
/// Renders the "Calibration" heading followed by an instruction prompt matching the current
/// calibration step, and invalidates the display fields so normal display resumes cleanly once
/// calibration completes.
/// </summary>
///
void renderCalibrationPrompt()
{
   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(TITLE_TEXT_SIZE);
   arduino.println("Calibration", Color::HEADING);
   arduino.moveCursorY(8);

   arduino.setTextSize(SUBTITLE_TEXT_SIZE);
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_MS5837
   if (calibrationState == CalibrationState::WaitFirstPoint)
   {
      arduino.println("Press A when", Color::LABEL);
      arduino.println("sensor is out of water", Color::LABEL);
   }
   else if (calibrationState == CalibrationState::WaitSecondPoint)
   {
      arduino.println("Press A when", Color::LABEL);
      arduino.println("sensor is fully submerged", Color::LABEL);
   }
#elif DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
   if (calibrationState == CalibrationState::WaitFirstPoint)
   {
      arduino.println("Press A when", Color::LABEL);
      arduino.println("sensor is in air", Color::LABEL);
   }
   else if (calibrationState == CalibrationState::WaitSecondPoint)
   {
      arduino.println("Press A when", Color::LABEL);
      arduino.println("sensor is submerged 18 in", Color::LABEL);
   }
#endif

   depthField->invalidate();
   rateField->invalidate();
   depthBar->reset();
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
   if (bufferSizeField != nullptr)
   {
      bufferSizeField->invalidate();
      bufferSizeField->draw();
   }
#endif
}

///
/// <summary>
/// Saves the calibration values to Preferences so they can be automatically restored the next
/// time the sketch starts.
/// </summary>
///
void saveCalibration()
{
   preferences.begin(PREFERENCES_NAMESPACE, false);
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_MS5837
   preferences.putFloat(BASELINE_DEPTH_KEY, baselineDepthCm);
   preferences.putFloat(MAX_DEPTH_KEY, maxDepthCm);
#elif DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
   preferences.putFloat(CAP_ZERO_TIME_KEY, capZeroChargeTime);
   preferences.putFloat(CAP_CALIBRATION_TIME_KEY, capCalibrationChargeTime);
#endif
   preferences.end();
}

///
/// <summary>
/// Loads a previously saved calibration from Preferences, falling back to defaults when no
/// calibration has been saved yet, and applies it to the sensor.
/// </summary>
/// <returns>True if a previously saved calibration was found and loaded; otherwise false.</returns>
///
bool loadCalibration()
{
   preferences.begin(PREFERENCES_NAMESPACE, true);
   bool hasCalibration;
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_MS5837
   hasCalibration = preferences.isKey(BASELINE_DEPTH_KEY) && preferences.isKey(MAX_DEPTH_KEY);
   baselineDepthCm = preferences.getFloat(BASELINE_DEPTH_KEY, baselineDepthCm);
   maxDepthCm = preferences.getFloat(MAX_DEPTH_KEY, maxDepthCm);
#elif DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
   hasCalibration = preferences.isKey(CAP_ZERO_TIME_KEY) && preferences.isKey(CAP_CALIBRATION_TIME_KEY);
   capZeroChargeTime = preferences.getFloat(CAP_ZERO_TIME_KEY, capZeroChargeTime);
   capCalibrationChargeTime = preferences.getFloat(CAP_CALIBRATION_TIME_KEY, capCalibrationChargeTime);
   sensor.setCalibration(capZeroChargeTime, capCalibrationChargeTime);
   capBufferSize = preferences.getInt(CAP_BUFFER_SIZE_KEY, capBufferSize);
   sensor.setBufferSize((size_t)capBufferSize);
#endif
   preferences.end();
   return hasCalibration;
}

#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
///
/// <summary>
/// Saves the current capacitor sensor buffer size to Preferences so it can be automatically
/// restored the next time the sketch starts.
/// </summary>
///
void saveBufferSize()
{
   preferences.begin(PREFERENCES_NAMESPACE, false);
   preferences.putInt(CAP_BUFFER_SIZE_KEY, capBufferSize);
   preferences.end();
}
#endif

///
/// <summary>
/// Draws the sketch heading and a subheading showing the sensor type, and returns the Y position
/// just below them.
/// </summary>
/// <returns>The Y coordinate immediately below the heading and subheading.</returns>
///
int16_t drawHeading()
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(TITLE_TEXT_SIZE);
   arduino.println("Water Depth", Color::HEADING);

   arduino.setTextSize(SUBTITLE_TEXT_SIZE);
   arduino.println((std::string("Sensor: ") + SENSOR_TYPE_NAME).c_str(), Color::LABEL);

   return arduino.getCursorY();
}

void setup()
{
   Wire.begin();
   SerialX::begin();
   arduino.begin();

   while (!sensor.begin())
   {
      arduino.setCursor(0, 0);
      arduino.setTextSize(TITLE_TEXT_SIZE);
      arduino.println("Sensor init failed", Color::RED);
      delay(1000);
   }

   bool hasCalibration = loadCalibration();

#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_MS5837
   baselineDepthCm = sensor.rawDepthCm();
   sensor.setBaseline(baselineDepthCm);
#endif

   arduino.clearDisplay();

   int16_t headerHeight = drawHeading();

   arduino.setTextSize(RATE_TEXT_SIZE);
   std::string rateSample(rateFormat.length(), '0');
   int16_t rateWidth = arduino.textWidth(rateSample.c_str());
   int16_t rateX = arduino.width() - rateWidth;
   int16_t rateY = arduino.height() - arduino.charH();

   arduino.setTextSize(DEPTH_TEXT_SIZE);
   std::string depthSample(deltaDepthFormat.length(), '0');
   int16_t depthWidth = arduino.textWidth(depthSample.c_str());
   int16_t depthX = (arduino.width() - depthWidth) / 2;
   int16_t depthY = headerHeight + ((rateY - headerHeight - arduino.charH()) / 2);
   depthField = new DisplayField(&arduino, depthX, depthY, "", deltaDepthFormat, DEPTH_TEXT_SIZE, true, Color::LABEL, Color::VALUE);

   arduino.setTextSize(RATE_TEXT_SIZE);
   rateField = new DisplayField(&arduino, rateX, rateY, "", rateFormat, RATE_TEXT_SIZE, true, Color::GRAY, Color::GRAY);

   Rect16 depthBarRect(arduino.width() - DEPTH_BAR_WIDTH_PX, 0, DEPTH_BAR_WIDTH_PX, rateY);
   depthBar = new VerticalBar(depthBarRect, RangeF(maxDepthCm, 0.0f), Color::BLUE, Color::BLACK);
   depthBar->reset(NAN);

#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
   arduino.setTextSize(SUBTITLE_TEXT_SIZE);
   std::string bufferSample(bufferSizeFormat.length(), '0');
   int16_t bufferWidth = arduino.textWidth(bufferSample.c_str());
   int16_t bufferX = arduino.width() - bufferWidth - DEPTH_BAR_WIDTH_PX;
   bufferSizeField = new DisplayField(&arduino, bufferX, 0, "", bufferSizeFormat, SUBTITLE_TEXT_SIZE, true, Color::GRAY, Color::GRAY);
   bufferSizeField->setValue((int)capBufferSize);
   bufferSizeField->draw();
#endif

   if (!hasCalibration)
   {
      calibrationState = CalibrationState::WaitFirstPoint;
      renderCalibrationPrompt();
   }
}

void loop()
{
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
   int32_t bufferDelta = arduino.encoderB.delta();
   if (calibrationState == CalibrationState::None && bufferDelta != 0)
   {
      capBufferSize = constrain(capBufferSize + bufferDelta * 10, CAPACITOR_BUFFER_SIZE_MIN, CAPACITOR_BUFFER_SIZE_MAX);
      sensor.setBufferSize((size_t)capBufferSize);
      saveBufferSize();
      bufferSizeField->setValue((int)capBufferSize);
      bufferSizeField->draw();
   }
#endif

   if (arduino.buttonA.wasPressed())
   {
      if (calibrationState == CalibrationState::None)
      {
         calibrationState = CalibrationState::WaitFirstPoint;
         renderCalibrationPrompt();
      }
      else if (calibrationState == CalibrationState::WaitFirstPoint)
      {
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_MS5837
         baselineDepthCm = sensor.rawDepthCm();
         sensor.setBaseline(baselineDepthCm);
#elif DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
         capZeroChargeTime = sensor.chargeTimeMicros();
#endif
         calibrationState = CalibrationState::WaitSecondPoint;
         renderCalibrationPrompt();
      }
      else if (calibrationState == CalibrationState::WaitSecondPoint)
      {
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_MS5837
         maxDepthCm = sensor.getDepth();
         depthBar->setRange(RangeF(maxDepthCm, 0.0f));
#elif DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
         capCalibrationChargeTime = sensor.chargeTimeMicros();
         sensor.setCalibration(capZeroChargeTime, capCalibrationChargeTime);
#endif
         saveCalibration();
         calibrationState = CalibrationState::None;
         arduino.clearDisplay();
         drawHeading();
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
         if (bufferSizeField != nullptr)
         {
            bufferSizeField->invalidate();
            bufferSizeField->draw();
         }
#endif
      }
      return;
   }

   if (calibrationState != CalibrationState::None)
   {
      return;
   }

   readRate.tick();

   float deltaDepthCm = sensor.getDepth();

   depthField->setValue(deltaDepthCm);
   depthField->draw();

   rateField->setValue(readRate.get());
   rateField->draw();

   depthBar->set(deltaDepthCm);
   depthBar->draw(&arduino.display);
}
