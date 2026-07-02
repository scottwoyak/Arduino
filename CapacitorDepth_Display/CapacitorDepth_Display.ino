#include <Arduino.h>
#include "CapacitorDepthSensor.h"
#include "Feather.h"
#include "RollingStats.h"
#include "Timer.h"

Feather feather;
Format depthFormat("###.# cm");
Format rateFormat("#### per/s");

constexpr unsigned long DISPLAY_UPDATE_MS = 100;
Timer displayTimer(DISPLAY_UPDATE_MS);

// Hardware Pin Assignments
const int CHARGE_PIN = 11;
const int SENSE_PIN = 5;

constexpr uint16_t SENSOR_DISCHARGE_DELAY_US = 200;
constexpr uint32_t SENSOR_DEFERRED_PERIOD_US = 500;
constexpr size_t SENSOR_BUFFER_SIZE = 100;

constexpr auto DEFAULT_ZERO_CHARGE_TIME = 128.3f;
constexpr auto DEFAULT_HALF_CHARGE_TIME = 295.0f;
constexpr auto HALF_DEPTH_INCHES = 18.0f;
constexpr auto FULL_DEPTH = HALF_DEPTH_INCHES * 2.0f;

const char PREF_NAMESPACE[] = "cap_depth";
const char PREF_ZERO_KEY[] = "zero";
const char PREF_HALF_KEY[] = "half";

enum class CalibrationState : uint8_t
{
   None,
   WaitAir,
   WaitHalf
};

CapacitorDepthSensor sensor(CHARGE_PIN, SENSE_PIN, DEFAULT_ZERO_CHARGE_TIME, DEFAULT_HALF_CHARGE_TIME, FULL_DEPTH);
CalibrationState calibrationState = CalibrationState::None;
CalibrationState lastRenderedCalibrationState = CalibrationState::None;
bool displayNeedsClear = true;
float calibrationZero = DEFAULT_ZERO_CHARGE_TIME;
float calibrationHalf = DEFAULT_HALF_CHARGE_TIME;

void saveCalibration(float zeroTime, float halfTime)
{
   feather.preferences.begin(PREF_NAMESPACE, false);
   feather.preferences.putString(PREF_ZERO_KEY, String(zeroTime, 6));
   feather.preferences.putString(PREF_HALF_KEY, String(halfTime, 6));
   feather.preferences.end();
}

void loadCalibration()
{
   feather.preferences.begin(PREF_NAMESPACE, true);
   String zeroText = feather.preferences.getString(PREF_ZERO_KEY, String(DEFAULT_ZERO_CHARGE_TIME, 6));
   String halfText = feather.preferences.getString(PREF_HALF_KEY, String(DEFAULT_HALF_CHARGE_TIME, 6));
   feather.preferences.end();

   calibrationZero = zeroText.toFloat();
   calibrationHalf = halfText.toFloat();
   sensor.setCalibration(calibrationZero, calibrationHalf);
}

void renderCalibrationPrompt(const char* line1, const char* line2)
{
   feather.setCursor(0, 0);
   feather.setTextSize(2);
   feather.println("Calibration", Color::HEADING);
   feather.moveCursorY(8);
   feather.println(line1, Color::LABEL);
   feather.println(line2, Color::LABEL);
}

void renderDepthDisplay()
{
   float depth = sensor.getDepth();

   feather.setCursor(0, 0);
   feather.setTextSize(2);
   feather.println("Capacitor Depth Sensor", Color::HEADING);
   feather.moveCursorY(10);

   feather.setTextSize(5);
   feather.println(depth, depthFormat, Color::VALUE);

   feather.setTextSize(3);
   feather.setCursorY(feather.height() - feather.charH());
   feather.printlnR(sensor.rate(), rateFormat, Color::SUB_LABEL);
}

void setup()
{
   feather.begin();
   sensor.begin();
   sensor.setDischargeDelayMicros(SENSOR_DISCHARGE_DELAY_US);
   sensor.setDeferredProcessingPeriodMicros(SENSOR_DEFERRED_PERIOD_US);
   sensor.setBufferSize(SENSOR_BUFFER_SIZE);
   loadCalibration();
}

void loop()
{
   if (calibrationState == CalibrationState::None)
   {
      if (feather.buttonA.wasPressed())
      {
         calibrationState = CalibrationState::WaitAir;
         displayNeedsClear = true;
      }
   }
   else if (feather.buttonA.wasPressed())
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
         feather.clearDisplay();
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
