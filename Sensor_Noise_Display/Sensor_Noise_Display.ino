#include "Feather.h"
#include "SerialX.h"
#include "RollingStats.h"
#include "RollingRate.h"
#include "Timer.h"

#define SENSOR_MODE_CAPACITOR 1

#if SENSOR_MODE_CAPACITOR
#include "CapacitorSensor.h"
#else
#include "TempSensor.h"
#endif

//
// This sketch continuously reads sensor temperature, shows rolling averages
// for multiple sample windows and displays the range of recent average values 
// for each window.
//
constexpr size_t NUM_ROLLING_SAMPLES_FOR_DISPLAYED_VALUE_RANGE = 100;

class Test
{
private:
   const char* _label = "";
   RollingStats _stats;
   RollingStats _averageValues = RollingStats(NUM_ROLLING_SAMPLES_FOR_DISPLAYED_VALUE_RANGE);
   uint32_t _samplesCollected = 0;

public:
   Test(const char* label, size_t sampleSize)
      : _label(label), _stats(sampleSize)
   {
   }

   void set(float value)
   {
      _samplesCollected++;
      _stats.set(value);

      if (_samplesCollected < _stats.size())
      {
         return;
      }

      float avg = _stats.average();
      if (!isfinite(avg))
      {
         return;
      }

      _averageValues.set(avg);
   }

   const char* label() const
   {
      return _label;
   }

   float average() const
   {
      return _stats.average();
   }

   bool isRangeReady() const
   {
      return _samplesCollected >= _stats.size();
   }

   float range() const
   {
      if (!isRangeReady())
      {
         return NAN;
      }

      return _averageValues.max() - _averageValues.min();
   }

   void reset()
   {
      _stats.reset();
      _averageValues.reset();
      _samplesCollected = 0;
   }
};

#if SENSOR_MODE_CAPACITOR
constexpr uint8_t CHARGE_PIN = 5;
constexpr uint8_t SENSE_PIN = 6;
#endif

Feather feather;
#if SENSOR_MODE_CAPACITOR
CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN, 500);
#else
TempSensor sensor;
RollingRate sampleRate(200);
#endif
Timer displayTimer(100);
Test tests[] =
{
   { "  N1", 1 },
   { " N10", 10 },
   { "N100", 100 },
   { "N500", 500 }
};
constexpr uint8_t NUM_TESTS = sizeof(tests) / sizeof(tests[0]);
uint32_t sampleCount = 0;

Format valueFormat("###.##");
Format sigmaFormat("##.###");
Format rateFormat("####/s", Format::Alignment::RIGHT);

void setup()
{
   SerialX::begin(115200, 1000);
   feather.begin();

   sensor.begin();
}

float readSensor()
{
#if SENSOR_MODE_CAPACITOR
   return (float)sensor.chargeTimeMicros();
#else
   return sensor.readTemperatureF();
#endif
}

float readSensorRate()
{
#if SENSOR_MODE_CAPACITOR
   return sensor.rate();
#else
   return sampleRate.get();
#endif
}

void loop()
{
   float value = readSensor();

#if !SENSOR_MODE_CAPACITOR
   sampleRate.tick();
#endif

   sampleCount++;
   for (uint8_t i = 0; i < NUM_TESTS; i++)
   {
      tests[i].set(value);
   }

   if (!displayTimer.ready())
   {
      return;
   }

   float sensorRate = readSensorRate();
   uint16_t samplesPerSecond = isfinite(sensorRate) ? (uint16_t)round(sensorRate) : 0;

   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Sensor Noise", Color::HEADING);
   feather.moveCursorY(2);

   for (uint8_t i = 0; i < NUM_TESTS; i++)
   {
      feather.print(tests[i].label(), Color::LABEL);
      feather.print(" ", Color::LABEL);

      feather.print(tests[i].average(), valueFormat, Color::VALUE);
      feather.print(" ");
      if (tests[i].isRangeReady())
      {
         feather.println(tests[i].range(), sigmaFormat, Color::VALUE2);
      }
      else
      {
         feather.println("----", Color::GRAY);
      }
      feather.moveCursorY(-4);
   }

   feather.setTextSize(2);
   feather.setCursorY(-feather.charH());
   feather.print("Samples: ", Color::GRAY);
   feather.print(sampleCount, Color::GRAY);
   feather.printR(samplesPerSecond, rateFormat, Color::GRAY);
}
