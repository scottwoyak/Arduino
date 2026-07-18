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
// centimeters centered below them with the equivalent value in inches shown just below it, each
// followed in gray by the 1-second rolling range (max-min) of that measurement (in millimeters
// for the centimeter row), labeled above with the sampling window, a blue depth level bar on the
// right (full at zero depth, empty at the calibrated maximum depth), and the live sensor read
// rate in gray in the lower right corner.
// For the capacitor sensor, the rolling-average buffer size is shown in the upper right corner and
// can be adjusted live with Encoder A (requires a Playground board); the outlier filter percent is
// shown just below it and can be adjusted live with Encoder B. Both values are persisted to
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
#include "TimedStats.h"
#include "Timer.h"

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
#error "The capacitor sensor's buffer size and filter controls require a Playground board (e.g. ESP32S3 Dev Module) with Encoder A and Encoder B."
#endif
#endif

constexpr float FRESH_WATER_DENSITY_KG_PER_M3 = 997.0f;

constexpr uint8_t CAPACITOR_CHARGE_PIN = CapacitorSensor::CHARGE_PIN_100K;
constexpr uint8_t CAPACITOR_SENSE_PIN = CapacitorSensor::SENSE_PIN;
constexpr float CAPACITOR_DEFAULT_ZERO_CHARGE_TIME = 128.3f;
constexpr float CAPACITOR_DEFAULT_CALIBRATION_CHARGE_TIME = 295.0f;
constexpr float CAPACITOR_CALIBRATION_DEPTH_CM = 45.72f; // 18 inches (half of full depth)
constexpr int32_t CAPACITOR_DEFAULT_BUFFER_SIZE = 30; // matches CapacitorSensor::DEFAULT_BUFFER_SIZE
constexpr int32_t CAPACITOR_BUFFER_SIZE_MIN = 1;
constexpr int32_t CAPACITOR_BUFFER_SIZE_MAX = 2000;
constexpr float CAPACITOR_DEFAULT_FILTER = 5.0f; // percent, per FilteredRollingAverage::FilterMode::PERCENT
constexpr float CAPACITOR_FILTER_MIN = 0.0f;
constexpr float CAPACITOR_FILTER_MAX = 100.0f;
constexpr float CAPACITOR_FILTER_STEP = 0.5f;

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
   CAPACITOR_CALIBRATION_DEPTH_CM);
constexpr auto SENSOR_TYPE_NAME = "Capacitor";
#endif

Preferences preferences;

constexpr auto PREFERENCES_NAMESPACE = "DepthDisplay";
constexpr auto BASELINE_DEPTH_KEY = "baselineCm";
constexpr auto MAX_DEPTH_KEY = "maxDepthCm";
constexpr auto CAP_ZERO_TIME_KEY = "capZeroTime";
constexpr auto CAP_CALIBRATION_TIME_KEY = "capCalibTime";
constexpr auto CAP_BUFFER_SIZE_KEY = "capBufSize";
constexpr auto CAP_FILTER_KEY = "capFilter";

Format depthCmFormat("####.# cm");
Format depthInFormat("####.# in");
Format depthCmRangeFormat("#### mm");
Format depthInRangeFormat("###.# in");
constexpr auto RATE_LABEL = "Sensor Sampling Rate: ";
Format bufferSizeFormat("Buf:####", Format::Alignment::RIGHT);
Format filterSizeFormat("Flt:##.#%", Format::Alignment::RIGHT);

constexpr float CENTIMETERS_PER_INCH = 2.54f;
constexpr float MILLIMETERS_PER_CENTIMETER = 10.0f;
constexpr unsigned long DEPTH_RANGE_WINDOW_MS = 1000;

constexpr float DEFAULT_FULL_DEPTH_FEET = 3.0f;
constexpr float CENTIMETERS_PER_FOOT = 30.48f;
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_MS5837
constexpr float DEFAULT_MAX_DEPTH_CM = DEFAULT_FULL_DEPTH_FEET * CENTIMETERS_PER_FOOT;
#elif DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
constexpr float DEFAULT_MAX_DEPTH_CM = 91.44f; // 36 inches
#endif
constexpr uint8_t TITLE_TEXT_SIZE = 3;
constexpr uint8_t SUBTITLE_TEXT_SIZE = 2;
constexpr uint8_t DEPTH_TEXT_SIZE = 4;
constexpr uint8_t DEPTH_RANGE_TEXT_SIZE = DEPTH_TEXT_SIZE;
constexpr uint8_t RANGE_LABEL_TEXT_SIZE = 2;
std::string rangeLabelText;
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
float capFilterSize = CAPACITOR_DEFAULT_FILTER;
RollingRate readRate;
CalibrationState calibrationState = CalibrationState::None;

TimedStats depthCmRangeStats(DEPTH_RANGE_WINDOW_MS);

constexpr float DISPLAY_REFRESH_RATE_HZ = 15.0f;
RateTimer displayTimer(DISPLAY_REFRESH_RATE_HZ);

DisplayField* depthCmField = nullptr;
DisplayField* depthInField = nullptr;
DisplayField* depthCmRangeField = nullptr;
DisplayField* depthInRangeField = nullptr;
DisplayField* bufferSizeField = nullptr;
DisplayField* filterSizeField = nullptr;
VerticalBar* depthBar = nullptr;

int16_t rangeLabelX = 0;
int16_t rangeLabelY = 0;

///
/// <summary>
/// Draws the small "Range (prior Ns)" label above the depth range values, indicating the
/// sampling window (in seconds) used to compute the rolling range.
/// </summary>
///
void drawRangeLabel()
{
   arduino.setTextSize(RANGE_LABEL_TEXT_SIZE);
   arduino.setCursor(rangeLabelX, rangeLabelY);
   arduino.println(rangeLabelText.c_str(), Color::GRAY);
}

///
/// <summary>
/// Draws the "Sensor Sampling Rate: #/s" row in the lower right corner, right-anchored so
/// the whole label+value row shifts left as the value's digit count grows, with no padding
/// gap between the label and the value.
/// </summary>
///
void drawRateRow(float effectiveRate)
{
   std::string rateValueText = std::to_string((long)round(effectiveRate)) + "/s";
   arduino.setTextSize(RATE_TEXT_SIZE);
   arduino.setCursor(0, arduino.height() - arduino.charH());
   arduino.printR(RATE_LABEL, rateValueText.c_str(), Color::GRAY, Color::GRAY, Color::BLACK);
}

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

   depthCmField->invalidate();
   depthInField->invalidate();
   depthCmRangeField->invalidate();
   depthInRangeField->invalidate();
   depthBar->reset();
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
   if (bufferSizeField != nullptr)
   {
      bufferSizeField->invalidate();
      bufferSizeField->draw((int)capBufferSize);
   }
   if (filterSizeField != nullptr)
   {
      filterSizeField->invalidate();
      filterSizeField->draw(capFilterSize);
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
   capFilterSize = preferences.getFloat(CAP_FILTER_KEY, capFilterSize);
   sensor.setFilter(capFilterSize, FilteredRollingAverage::FilterMode::PERCENT);
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

///
/// <summary>
/// Saves the current capacitor sensor filter value to Preferences so it can be automatically
/// restored the next time the sketch starts.
/// </summary>
///
void saveFilterSize()
{
   preferences.begin(PREFERENCES_NAMESPACE, false);
   preferences.putFloat(CAP_FILTER_KEY, capFilterSize);
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
   int16_t rateY = arduino.height() - arduino.charH();

   arduino.setTextSize(DEPTH_TEXT_SIZE);
   std::string depthCmSample(depthCmFormat.length(), '0');
   int16_t depthCmWidth = arduino.textWidth(depthCmSample.c_str());
   int16_t depthCmHeight = arduino.charH();

   arduino.setTextSize(DEPTH_TEXT_SIZE);
   std::string depthInSample(depthInFormat.length(), '0');
   int16_t depthInWidth = arduino.textWidth(depthInSample.c_str());
   int16_t depthInHeight = arduino.charH();

   arduino.setTextSize(DEPTH_RANGE_TEXT_SIZE);
   std::string depthCmRangeSample(depthCmRangeFormat.length(), '0');
   int16_t depthCmRangeWidth = arduino.textWidth(depthCmRangeSample.c_str());
   int16_t depthCmRangeHeight = arduino.charH();

   std::string depthInRangeSample(depthInRangeFormat.length(), '0');
   int16_t depthInRangeWidth = arduino.textWidth(depthInRangeSample.c_str());
   int16_t depthInRangeHeight = arduino.charH();

   arduino.setTextSize(RANGE_LABEL_TEXT_SIZE);
   rangeLabelText = "Range (prior " + std::to_string(DEPTH_RANGE_WINDOW_MS / 1000) + "s)";
   int16_t rangeLabelHeight = arduino.charH();

   constexpr int16_t DEPTH_FIELD_GAP_PX = 4;
   constexpr int16_t DEPTH_RANGE_GAP_PX = 8;
   constexpr int16_t RANGE_LABEL_GAP_PX = 4;
   int16_t depthBlockHeight = depthCmHeight + DEPTH_FIELD_GAP_PX + depthInHeight;
   int16_t depthCmY = headerHeight + ((rateY - headerHeight - depthBlockHeight) / 2);
   int16_t depthInY = depthCmY + depthCmHeight + DEPTH_FIELD_GAP_PX;

   int16_t depthRowWidth = depthCmWidth + DEPTH_RANGE_GAP_PX + depthCmRangeWidth;
   int16_t depthInRowWidth = depthInWidth + DEPTH_RANGE_GAP_PX + depthInRangeWidth;
   int16_t depthBlockWidth = max(depthRowWidth, depthInRowWidth);
   int16_t depthX = (arduino.width() - depthBlockWidth) / 2;
   int16_t depthCmX = depthX;
   int16_t depthInX = depthX;
   int16_t depthCmRangeY = depthCmY + ((depthCmHeight - depthCmRangeHeight) / 2);

   int16_t depthInRangeY = depthInY + ((depthInHeight - depthInRangeHeight) / 2);

   int16_t depthCmRangeX = max(depthCmX + depthCmWidth + DEPTH_RANGE_GAP_PX, depthInX + depthInWidth + DEPTH_RANGE_GAP_PX);
   int16_t depthInRangeX = depthCmRangeX;

   rangeLabelX = depthCmRangeX;
   rangeLabelY = depthCmY - RANGE_LABEL_GAP_PX - rangeLabelHeight;

   depthCmField = new DisplayField(&arduino, depthCmX, depthCmY, "", depthCmFormat, DEPTH_TEXT_SIZE);
   depthInField = new DisplayField(&arduino, depthInX, depthInY, "", depthInFormat, DEPTH_TEXT_SIZE);
   depthCmRangeField = new DisplayField(&arduino, depthCmRangeX, depthCmRangeY, "", depthCmRangeFormat, DEPTH_RANGE_TEXT_SIZE, Color::GRAY, Color::GRAY);
   depthInRangeField = new DisplayField(&arduino, depthInRangeX, depthInRangeY, "", depthInRangeFormat, DEPTH_RANGE_TEXT_SIZE, Color::GRAY, Color::GRAY);

   drawRangeLabel();
   drawRateRow(0.0f);

   Rect16 depthBarRect(arduino.width() - DEPTH_BAR_WIDTH_PX, 0, DEPTH_BAR_WIDTH_PX, rateY);
   depthBar = new VerticalBar(depthBarRect, RangeF(maxDepthCm, 0.0f), Color::BLUE, Color::BLACK);
   depthBar->reset(NAN);

#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
   arduino.setTextSize(SUBTITLE_TEXT_SIZE);
   std::string bufferSample(bufferSizeFormat.length(), '0');
   int16_t bufferWidth = arduino.textWidth(bufferSample.c_str());
   int16_t bufferX = arduino.width() - bufferWidth - DEPTH_BAR_WIDTH_PX;
   bufferSizeField = new DisplayField(&arduino, bufferX, 0, "", bufferSizeFormat, SUBTITLE_TEXT_SIZE, Color::GRAY, Color::GRAY);
   bufferSizeField->draw((int)capBufferSize);

   std::string filterSample(filterSizeFormat.length(), '0');
   int16_t filterWidth = arduino.textWidth(filterSample.c_str());
   int16_t filterX = arduino.width() - filterWidth - DEPTH_BAR_WIDTH_PX;
   int16_t filterY = arduino.charH();
   filterSizeField = new DisplayField(&arduino, filterX, filterY, "", filterSizeFormat, SUBTITLE_TEXT_SIZE, Color::GRAY, Color::GRAY);
   filterSizeField->draw(capFilterSize);
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
   int32_t bufferDelta = arduino.encoderA.delta();
   if (calibrationState == CalibrationState::None && bufferDelta != 0)
   {
      int32_t bufferStep = (capBufferSize >= 1000) ? 100 : (capBufferSize >= 100) ? 50 : 10;
      capBufferSize = constrain(capBufferSize + bufferDelta * bufferStep, CAPACITOR_BUFFER_SIZE_MIN, CAPACITOR_BUFFER_SIZE_MAX);
      sensor.setBufferSize((size_t)capBufferSize);
      saveBufferSize();
      bufferSizeField->draw((int)capBufferSize);
   }

   int32_t filterDelta
   if (calibrationState == CalibrationState::None && filterDelta != 0)
   {
      capFilterSize = constrain(capFilterSize + filterDelta * CAPACITOR_FILTER_STEP, CAPACITOR_FILTER_MIN, CAPACITOR_FILTER_MAX);
      sensor.setFilter(capFilterSize, FilteredRollingAverage::FilterMode::PERCENT);
      saveFilterSize();
      filterSizeField->draw(capFilterSize);
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
         drawRangeLabel();
         drawRateRow(0.0f);
#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
         if (bufferSizeField != nullptr)
         {
            bufferSizeField->invalidate();
            bufferSizeField->draw((int)capBufferSize);
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
   depthCmRangeStats.set(deltaDepthCm);

#if DEPTH_SENSOR_TYPE == DEPTH_SENSOR_CAPACITOR
   float effectiveRate = sensor.effectiveRate();
#else
   float effectiveRate = readRate.get();
#endif

   if (displayTimer.ready())
   {
      float depthCmRange = depthCmRangeStats.range();

      depthCmField->draw(deltaDepthCm);

      depthInField->draw(deltaDepthCm / CENTIMETERS_PER_INCH);

      depthCmRangeField->draw(depthCmRange * MILLIMETERS_PER_CENTIMETER);

      depthInRangeField->draw(depthCmRange / CENTIMETERS_PER_INCH);

      drawRateRow(effectiveRate);

      depthBar->set(deltaDepthCm);
      depthBar->draw(&arduino.display);
   }
}
