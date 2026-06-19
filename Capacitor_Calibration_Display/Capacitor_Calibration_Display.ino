#include <Arduino.h>
#include <CapacitorDepthSensor.h>
#include <Feather.h>
#include <RollingStats.h>
#include <Timer.h>

Feather feather;
Format dischargeDelayMicrosFormat("### us");
Format chargeFormat("###.# us");
Format effectiveRateFormat("###/s");
Format rawRateFormat("######/s");
Format rangeFormat("##.# us");
Format varianceFormat("#.## in");
Format timeFormat("###.# us");

constexpr size_t DISPLAY_INTERVAL_MS = 100;
constexpr float DISPLAYED_STATS_WINDOW_SECS = 2.0f;
constexpr unsigned long DISPLAYED_STATS_WINDOW_MS = (unsigned long)(DISPLAYED_STATS_WINDOW_SECS * 1000.0f);
constexpr size_t DISPLAYED_SAMPLE_COUNT_RAW = (size_t)(DISPLAYED_STATS_WINDOW_MS / DISPLAY_INTERVAL_MS);
constexpr size_t DISPLAYED_SAMPLE_COUNT = (DISPLAYED_SAMPLE_COUNT_RAW > 0) ? DISPLAYED_SAMPLE_COUNT_RAW : 1;

constexpr uint16_t TEST_EMPTY_DELAY_MIN_US = 10;
constexpr uint16_t TEST_EMPTY_DELAY_MAX_US = 200;
constexpr uint16_t TEST_EMPTY_DELAY_STEP_US = 10;
constexpr float TEST_EMPTY_DELAY_STEP_GROWTH = 1.1f;
constexpr unsigned long TEST_DURATION_MS = 2000;
constexpr unsigned long TEST_DISPLAYED_STATS_WINDOW_MS = TEST_DURATION_MS / 2;
constexpr size_t TEST_DISPLAYED_SAMPLE_COUNT_RAW = (size_t)(TEST_DISPLAYED_STATS_WINDOW_MS / DISPLAY_INTERVAL_MS);
constexpr size_t TEST_DISPLAYED_SAMPLE_COUNT = (TEST_DISPLAYED_SAMPLE_COUNT_RAW > 0) ? TEST_DISPLAYED_SAMPLE_COUNT_RAW : 1;

constexpr size_t RAW_SENSOR_STATS_SIZES[] = { 10, 20, 50, 100, 200, 500 };
constexpr size_t RAW_SENSOR_STATS_SIZE_COUNT = sizeof(RAW_SENSOR_STATS_SIZES) / sizeof(RAW_SENSOR_STATS_SIZES[0]);

Timer displayTimer(DISPLAY_INTERVAL_MS);
RollingStats rawSensorStats(RAW_SENSOR_STATS_SIZES[0]);

// Hardware Pin Assignments
const int CHARGE_PIN = 5;
const int SENSE_PIN = 6;

CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN);

// Rolling stats for displayed (smoothed) value over a fixed time window
RollingStats displayedStats(DISPLAYED_SAMPLE_COUNT);

bool testRunning = false;
uint16_t currentTestDelayUs = TEST_EMPTY_DELAY_MIN_US;
uint16_t currentTestStepUs = TEST_EMPTY_DELAY_STEP_US;
unsigned long currentTestStartMs = 0;
size_t currentTestNumber = 0;
size_t currentRawSensorStatsSizeIndex = 0;

size_t calculateDelayTestCount()
{
   size_t total = 1;
   uint16_t delayUs = TEST_EMPTY_DELAY_MIN_US;
   uint16_t stepUs = TEST_EMPTY_DELAY_STEP_US;

   while (delayUs < TEST_EMPTY_DELAY_MAX_US)
   {
      uint16_t nextStepUs = (uint16_t)(stepUs * TEST_EMPTY_DELAY_STEP_GROWTH + 0.5f);
      if (nextStepUs <= stepUs)
      {
         nextStepUs = stepUs + 1;
      }

      stepUs = nextStepUs;
      uint32_t nextDelayUs = delayUs + stepUs;
      delayUs = (nextDelayUs > TEST_EMPTY_DELAY_MAX_US) ? TEST_EMPTY_DELAY_MAX_US : (uint16_t)nextDelayUs;
      total++;
   }

   return total;
}

const size_t DELAY_TEST_COUNT = calculateDelayTestCount();
const size_t TOTAL_TESTS = DELAY_TEST_COUNT * RAW_SENSOR_STATS_SIZE_COUNT;

void resetTestStats()
{
   sensor.resetRate();
   rawSensorStats.reset();
   displayedStats.reset();
}

void resetDelaySweep()
{
   currentTestDelayUs = TEST_EMPTY_DELAY_MIN_US;
   currentTestStepUs = TEST_EMPTY_DELAY_STEP_US;
   sensor.setDischargeDelayMicros(currentTestDelayUs);
   resetTestStats();
   currentTestStartMs = millis();
}

void startDelayTest()
{
   testRunning = true;
   currentRawSensorStatsSizeIndex = 0;
   rawSensorStats.resize(RAW_SENSOR_STATS_SIZES[currentRawSensorStatsSizeIndex]);
   displayedStats.resize(TEST_DISPLAYED_SAMPLE_COUNT);
   currentTestNumber = 1;
   resetDelaySweep();

   Serial.println();
   Serial.println("Discharge Delay, Raw Rate, Effective Rate, Variance");
   Serial.print("\nNumber of Samples Averaged: ");
   Serial.print(rawSensorStats.size());
   Serial.println();

   feather.clearDisplay();
}

void setupNextDelayTest()
{
   uint16_t nextStepUs = (uint16_t)(currentTestStepUs * TEST_EMPTY_DELAY_STEP_GROWTH + 0.5f);
   if (nextStepUs <= currentTestStepUs)
   {
      nextStepUs = currentTestStepUs + 1;
   }

   currentTestStepUs = nextStepUs;

   uint32_t nextDelayUs = currentTestDelayUs + currentTestStepUs;
   currentTestDelayUs = (nextDelayUs > TEST_EMPTY_DELAY_MAX_US) ? TEST_EMPTY_DELAY_MAX_US : (uint16_t)nextDelayUs;

   currentTestNumber++;
   sensor.setDischargeDelayMicros(currentTestDelayUs);
   resetTestStats();
   currentTestStartMs = millis();
}

void setupNextStatsSizeTest()
{
   currentRawSensorStatsSizeIndex++;
   rawSensorStats.resize(RAW_SENSOR_STATS_SIZES[currentRawSensorStatsSizeIndex]);
   currentTestNumber++;
   resetDelaySweep();
   Serial.print("\nNumber of Samples Averaged: ");
   Serial.print(rawSensorStats.size());
   Serial.println();
}

void completeCurrentDelayTest()
{
   float rawRate = sensor.rate();
   float effectiveRate = (rawSensorStats.count() > 0) ? (rawRate / rawSensorStats.count()) : 0;
   float accuracy = displayedStats.range() / 65.0f * 60.0f;

   Serial.print(currentTestDelayUs);
   Serial.print("\t");
   Serial.print(rawRate, 0);
   Serial.print("/s");
   Serial.print("\t");
   Serial.print(effectiveRate);
   Serial.print("/s");
   Serial.print("\t");
   Serial.print(accuracy, 2);
   Serial.print(" in");
   Serial.println();

   if (currentTestDelayUs >= TEST_EMPTY_DELAY_MAX_US)
   {
      if (currentRawSensorStatsSizeIndex + 1 >= RAW_SENSOR_STATS_SIZE_COUNT)
      {
         testRunning = false;
         sensor.setDischargeDelayMicros(500);
         rawSensorStats.resize(100);
         displayedStats.resize(DISPLAYED_SAMPLE_COUNT);
         Serial.println("Testing Complete");
         feather.clearDisplay();
      }
      else
      {
         setupNextStatsSizeTest();
      }
   }
   else
   {
      setupNextDelayTest();
   }
}

void setup()
{
   Serial.begin(115200);
   feather.begin();
   sensor.setDischargeDelayMicros(500);
   sensor.begin();
}

void loop()
{
   if (feather.buttonA.wasPressed() && !testRunning)
   {
      startDelayTest();
   }

   if (sensor.hasChanged())
   {
      rawSensorStats.set(sensor.chargeTimeMicros());
   }

   if (testRunning)
   {
      if (millis() - currentTestStartMs >= TEST_DURATION_MS)
      {
         completeCurrentDelayTest();
      }
   }

   if (displayTimer.ready())
   {
      float displayedValue = rawSensorStats.average();
      displayedStats.set(displayedValue);

      feather.setCursor(0, 0);
      feather.setTextSize(2);
      if (testRunning)
      {
         feather.print("Testing ", Color::HEADING);
         feather.print(currentTestNumber, Color::HEADING2);
         feather.print("/", Color::HEADING2);
         feather.println(TOTAL_TESTS, Color::HEADING2);
      }
      else
      {
         feather.println("Capacitor Sensor Tuner", Color::HEADING);
      }

      feather.setTextSize(2);

      // since we are taking an average, the real sample rate is the time over the full period
      float effectiveRate = (rawSensorStats.count() > 0) ? (sensor.rate() / rawSensorStats.count()) : 0;

      // if we assume the charge time doubles when the capacitor is fully submerged,
      // then we can estimate errors as follows:
      constexpr auto DELTA_CHARGE_TIME_US = 65.0f; // empirically observed difference in charge time between empty and full   
      constexpr auto SENSOR_LENGTH = 60.0f; // length of the sensor in inches
      float variance = displayedStats.range() / DELTA_CHARGE_TIME_US * SENSOR_LENGTH;

      feather.print("     Empty Time: ", Color::LABEL);
      feather.print(sensor.dischargeDelayMicros(), dischargeDelayMicrosFormat, Color::VALUE2);
      feather.println();

      feather.print("    Num Samples: ", Color::LABEL);
      feather.print(rawSensorStats.count(), Color::VALUE2);
      feather.println();

      feather.print("Avg Charge Time: ", Color::LABEL);
      feather.print(displayedValue, chargeFormat, Color::VALUE);
      feather.println();

      feather.print("      Variation: ", Color::LABEL);
      feather.print(displayedStats.range(), rangeFormat, Color::VALUE);
      feather.println();

      feather.print("       Raw Rate: ", Color::LABEL);
      //Serial.println(sensor.rate());
      feather.print(sensor.rate(), rawRateFormat, Color::VALUE);
      feather.println();

      feather.print(" Effective Rate: ", Color::LABEL);
      feather.print(effectiveRate, effectiveRateFormat, Color::VALUE3);
      feather.println();

      feather.print("       Variance: ", Color::LABEL);
      feather.print(variance, varianceFormat, Color::VALUE3);
      feather.println();
   }
}
