//
// Displays MS5837 water depth delta in centimeters relative to a calibrated baseline.
//
// Initializes the I2C bus, Feather display, and MS5837 pressure sensor, retrying sensor
// initialization until successful and showing a visible error message on display in the meantime.
// Uses fresh-water density for depth conversion and stores startup depth as the initial baseline,
// then continuously reads depth and displays delta depth (currentDepthCm - baselineDepthCm).
//
// Calibration flow (Button A): pressing button A starts a calibration prompt asking the user to
// take the sensor out of the water; pressing button A again captures that reading as the new
// baseline and prompts the user to fully submerge the sensor; pressing button A a third time
// captures that reading as the new maximum depth (used to scale the depth bar), saves both
// values to Preferences, and resumes normal display. The saved baseline and maximum depth are
// automatically restored on startup. If no calibration has been saved yet, the calibration
// sequence starts automatically on startup.
//
// Display output: a heading and sensor-type subheading at the top, the signed depth delta in
// centimeters centered below them, a blue depth level bar on the right (full at zero depth, empty
// at the calibrated maximum depth), and the live sensor read rate in gray in the lower right corner.
//

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <MS5837.h>
#include "ArduinoBoard.h"
#include "Bar.h"
#include "DisplayField.h"
#include "RollingRate.h"
#include "SerialX.h"

#ifndef ARDUINO_BUTTON_A_SUPPORTED
#error "This sketch requires a board with a Button A (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

Arduino arduino;
MS5837 sensor;
Preferences preferences;

constexpr auto PREFERENCES_NAMESPACE = "MS5837Depth";
constexpr auto BASELINE_DEPTH_KEY = "baselineCm";
constexpr auto MAX_DEPTH_KEY = "maxDepthCm";

Format deltaDepthFormat("####.## cm");
Format rateFormat("##/s");

constexpr float FRESH_WATER_DENSITY_KG_PER_M3 = 997.0f;
constexpr float METERS_TO_CENTIMETERS = 100.0f;
constexpr float DEFAULT_FULL_DEPTH_FEET = 3.0f;
constexpr float CENTIMETERS_PER_FOOT = 30.48f;
constexpr float DEFAULT_MAX_DEPTH_CM = DEFAULT_FULL_DEPTH_FEET * CENTIMETERS_PER_FOOT;
constexpr uint8_t TITLE_TEXT_SIZE = 3;
constexpr uint8_t SUBTITLE_TEXT_SIZE = 2;
constexpr uint8_t DEPTH_TEXT_SIZE = 5;
constexpr uint8_t RATE_TEXT_SIZE = 2;
constexpr uint16_t DEPTH_BAR_WIDTH_PX = 10;
constexpr auto SENSOR_TYPE_NAME = "MS5837-02BA";

///
/// <summary>
/// Tracks progress through the calibration sequence.
/// </summary>
///
enum class CalibrationState : uint8_t
{
   None,
   WaitOutOfWater,
   WaitSubmerged
};

float baselineDepthCm = NAN;
float maxDepthCm = DEFAULT_MAX_DEPTH_CM;
RollingRate readRate;
CalibrationState calibrationState = CalibrationState::None;

DisplayField* depthField = nullptr;
DisplayField* rateField = nullptr;
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
   if (calibrationState == CalibrationState::WaitOutOfWater)
   {
      arduino.println("Press A when", Color::LABEL);
      arduino.println("sensor is out of water", Color::LABEL);
   }
   else if (calibrationState == CalibrationState::WaitSubmerged)
   {
      arduino.println("Press A when", Color::LABEL);
      arduino.println("sensor is fully submerged", Color::LABEL);
   }

   depthField->invalidate();
   rateField->invalidate();
   depthBar->reset();
}

///
/// <summary>
/// Saves the calibrated baseline and maximum depth values to Preferences so they can be
/// automatically restored the next time the sketch starts.
/// </summary>
///
void saveCalibration()
{
   preferences.begin(PREFERENCES_NAMESPACE, false);
   preferences.putFloat(BASELINE_DEPTH_KEY, baselineDepthCm);
   preferences.putFloat(MAX_DEPTH_KEY, maxDepthCm);
   preferences.end();
}

///
/// <summary>
/// Loads a previously calibrated baseline and maximum depth from Preferences, falling back to
/// the given defaults when no calibration has been saved yet.
/// </summary>
/// <returns>True if a previously saved calibration was found and loaded; otherwise false.</returns>
///
bool loadCalibration()
{
   preferences.begin(PREFERENCES_NAMESPACE, true);
   bool hasCalibration = preferences.isKey(BASELINE_DEPTH_KEY) && preferences.isKey(MAX_DEPTH_KEY);
   baselineDepthCm = preferences.getFloat(BASELINE_DEPTH_KEY, baselineDepthCm);
   maxDepthCm = preferences.getFloat(MAX_DEPTH_KEY, maxDepthCm);
   preferences.end();
   return hasCalibration;
}

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

   while (!sensor.init())
   {
      arduino.setCursor(0, 0);
      arduino.setTextSize(TITLE_TEXT_SIZE);
      arduino.println("MS5837 init failed", Color::RED);
      delay(1000);
   }

   sensor.setModel(MS5837::MS5837_02BA);
   sensor.setFluidDensity(FRESH_WATER_DENSITY_KG_PER_M3);

   sensor.read();
   baselineDepthCm = sensor.depth() * METERS_TO_CENTIMETERS;

   bool hasCalibration = loadCalibration();

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

   if (!hasCalibration)
   {
      calibrationState = CalibrationState::WaitOutOfWater;
      renderCalibrationPrompt();
   }
}

void loop()
{
   if (arduino.buttonA.wasPressed())
   {
      if (calibrationState == CalibrationState::None)
      {
         calibrationState = CalibrationState::WaitOutOfWater;
         renderCalibrationPrompt();
      }
      else if (calibrationState == CalibrationState::WaitOutOfWater)
      {
         sensor.read();
         baselineDepthCm = sensor.depth() * METERS_TO_CENTIMETERS;
         calibrationState = CalibrationState::WaitSubmerged;
         renderCalibrationPrompt();
      }
      else if (calibrationState == CalibrationState::WaitSubmerged)
      {
         sensor.read();
         float submergedDepthCm = (sensor.depth() * METERS_TO_CENTIMETERS) - baselineDepthCm;
         maxDepthCm = submergedDepthCm;
         depthBar->setRange(RangeF(maxDepthCm, 0.0f));
         saveCalibration();
         calibrationState = CalibrationState::None;
         arduino.clearDisplay();
         drawHeading();
      }
      return;
   }

   if (calibrationState != CalibrationState::None)
   {
      return;
   }

   sensor.read();
   readRate.tick();

   float currentDepthCm = sensor.depth() * METERS_TO_CENTIMETERS;
   float deltaDepthCm = currentDepthCm - baselineDepthCm;

   depthField->setValue(deltaDepthCm);
   depthField->draw();

   rateField->setValue(readRate.get());
   rateField->draw();

   depthBar->set(deltaDepthCm);
   depthBar->draw(&arduino.display);
}
