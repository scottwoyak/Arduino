/// <summary>
/// Displays capacitor-based water depth, sample rate, and a vertical level bar on a Feather display.
/// </summary>
/// <remarks>
/// Converts a rolling-average capacitor charge time into depth via CapacitorDepthSensor, using a
/// two-point (zero-depth and half-depth) calibration that is persisted with Preferences and reloaded
/// at startup. Pressing Button A starts a calibration sequence: the first press captures the zero-depth
/// (in air) charge time, the second press captures the half-depth charge time, after which the new
/// calibration is saved and normal depth display resumes.
/// </remarks>

#include <Arduino.h>
#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_PREFERENCES_SUPPORTED
#error "This sketch requires a board with Preferences support (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "Bar.h"
#include "CapacitorDepthSensor.h"
#include "Timer.h"

// ----------- Display
Arduino arduino;
Format depthFormat("###.# cm");
Format rateFormat("####/s");
Format effectiveRateFormat("####/s");
constexpr uint16_t DISPLAY_UPDATE_RATE_PER_SEC = 20;
constexpr uint16_t LEVEL_BAR_WIDTH_PX = 10;
Timer displayTimer(
   (DISPLAY_UPDATE_RATE_PER_SEC == 0) ? 0 :
   ((1000U / DISPLAY_UPDATE_RATE_PER_SEC) == 0 ? 1 : (1000U / DISPLAY_UPDATE_RATE_PER_SEC)));

// ----------- Pins
constexpr uint8_t CHARGE_PIN = 10;
constexpr uint8_t SENSE_PIN = 5;

// ----------- Sensor Setup
constexpr uint16_t SENSOR_DISCHARGE_DELAY_US = 200;
constexpr size_t SENSOR_BUFFER_SIZE = 100;

// Fallback calibration (charge time in microseconds) used until the user calibrates via Button A;
// the calibrated values are then persisted to Preferences and reloaded at startup.
constexpr auto DEFAULT_ZERO_CHARGE_TIME = 128.3f;
constexpr auto DEFAULT_HALF_CHARGE_TIME = 295.0f;

// Physical depth range: HALF_DEPTH_INCHES is the reference depth used to collect the half-depth
// calibration point, and FULL_DEPTH is the resulting total measurable depth range.
constexpr auto HALF_DEPTH_INCHES = 18.0f;
constexpr auto FULL_DEPTH = HALF_DEPTH_INCHES * 2.0f;

// ----------- Preferences Keys
constexpr const char* PREF_NAMESPACE = "cap_depth";
constexpr const char* PREF_ZERO_KEY = "zero";
constexpr const char* PREF_HALF_KEY = "half";

///
/// <summary>
/// Tracks progress through the two-point depth calibration sequence.
/// </summary>
///
enum class CalibrationState : uint8_t
{
   None,
   WaitAir,
   WaitHalf
};

CapacitorDepthSensor sensor(CHARGE_PIN, SENSE_PIN, DEFAULT_ZERO_CHARGE_TIME, DEFAULT_HALF_CHARGE_TIME, FULL_DEPTH);
VerticalBar* levelBar = nullptr;
CalibrationState calibrationState = CalibrationState::None;
CalibrationState lastRenderedCalibrationState = CalibrationState::None;
bool displayNeedsClear = true;
float calibrationZero = DEFAULT_ZERO_CHARGE_TIME;
float calibrationHalf = DEFAULT_HALF_CHARGE_TIME;

///
/// <summary>
/// Persists the zero-depth and half-depth calibration charge times to Preferences.
/// </summary>
/// <param name="zeroTime">Charge time (microseconds) measured at zero depth.</param>
/// <param name="halfTime">Charge time (microseconds) measured at half depth.</param>
///
void saveCalibration(float zeroTime, float halfTime)
{
   arduino.preferences.begin(PREF_NAMESPACE, false);
   arduino.preferences.putString(PREF_ZERO_KEY, String(zeroTime, 6));
   arduino.preferences.putString(PREF_HALF_KEY, String(halfTime, 6));
   arduino.preferences.end();
}

///
/// <summary>
/// Loads calibration charge times from Preferences (falling back to defaults) and applies them to the sensor.
/// </summary>
///
void loadCalibration()
{
   arduino.preferences.begin(PREF_NAMESPACE, true);
   String zeroText = arduino.preferences.getString(PREF_ZERO_KEY, String(DEFAULT_ZERO_CHARGE_TIME, 6));
   String halfText = arduino.preferences.getString(PREF_HALF_KEY, String(DEFAULT_HALF_CHARGE_TIME, 6));
   arduino.preferences.end();

   calibrationZero = zeroText.toFloat();
   calibrationHalf = halfText.toFloat();
   sensor.setCalibration(calibrationZero, calibrationHalf);
}

///
/// <summary>
/// Renders the "Calibration" heading followed by a two-line instruction prompt.
/// </summary>
/// <param name="line1">First line of instruction text.</param>
/// <param name="line2">Second line of instruction text.</param>
///
void renderCalibrationPrompt(const char* line1, const char* line2)
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(2);
   arduino.println("Calibration", Color::HEADING);
   arduino.moveCursorY(8);
   arduino.println(line1, Color::LABEL);
   arduino.println(line2, Color::LABEL);
}

///
/// <summary>
/// Renders the current depth, raw/effective sample rates, and the vertical level bar.
/// </summary>
///
void renderDepthDisplay()
{
   float depth = sensor.getDepth();

   arduino.setCursor(0, 0);
   arduino.setTextSize(2);
   arduino.println("Capacitor Depth Sensor", Color::HEADING);
   arduino.moveCursorY(10);

   arduino.setTextSize(5);
   arduino.println(depth, depthFormat, Color::VALUE);

   float rawRate = sensor.rate();
   float effectiveRate = (sensor.bufferSize() > 0) ? (rawRate / (float)sensor.bufferSize()) : 0.0f;

   arduino.setTextSize(3);
   arduino.setCursorY(arduino.height() - (arduino.charH() * 2));
   arduino.println(effectiveRate, effectiveRateFormat, Color::VALUE2);
   arduino.moveCursorY(-2);
   arduino.println(rawRate, rateFormat, Color::SUB_LABEL);

   if (levelBar != nullptr)
   {
      float clampedDepth = constrain(depth, 0.0f, FULL_DEPTH);
      levelBar->set(clampedDepth);
      levelBar->draw(&arduino.display);
   }
}

void setup()
{
   arduino.begin();
   sensor.begin();
   sensor.setDischargeDelayMicros(SENSOR_DISCHARGE_DELAY_US);
   sensor.setBufferSize(SENSOR_BUFFER_SIZE);
   Rect16 levelBarRect(arduino.width() - LEVEL_BAR_WIDTH_PX, 0, LEVEL_BAR_WIDTH_PX, arduino.height());
   levelBar = new VerticalBar(levelBarRect, RangeF(0.0f, FULL_DEPTH), Color::BLUE, Color::BLACK);
   levelBar->reset(NAN);
   loadCalibration();
}

void loop()
{
   if (calibrationState == CalibrationState::None)
   {
      if (arduino.buttonA.wasPressed())
      {
         calibrationState = CalibrationState::WaitAir;
         displayNeedsClear = true;
      }
   }
   else if (arduino.buttonA.wasPressed())
   {
      if (calibrationState == CalibrationState::WaitAir)
      {
         calibrationZero = sensor.chargeTimeMicros();
         calibrationState = CalibrationState::WaitHalf;
         displayNeedsClear = true;
      }
      else if (calibrationState == CalibrationState::WaitHalf)
      {
         calibrationHalf = sensor.chargeTimeMicros();
         sensor.setCalibration(calibrationZero, calibrationHalf);
         saveCalibration(calibrationZero, calibrationHalf);
         calibrationState = CalibrationState::None;
         displayNeedsClear = true;
      }
   }

   if (displayTimer.ready())
   {
      if (displayNeedsClear || (lastRenderedCalibrationState != calibrationState))
      {
         if (levelBar != nullptr)
         {
            levelBar->reset(NAN);
         }
         displayNeedsClear = false;
         lastRenderedCalibrationState = calibrationState;
      }

      if (calibrationState == CalibrationState::WaitAir)
      {
         renderCalibrationPrompt("Press A when", "sensor is in air");
         return;
      }

      if (calibrationState == CalibrationState::WaitHalf)
      {
         renderCalibrationPrompt("Press A at", "half depth (18in)");
         return;
      }

      renderDepthDisplay();
   }
}
