#include "Feather.h"
#include "Stopwatch.h"
#include "SerialX.h"
#include "CapacitorSensor.h"
#include "RollingStats.h"

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

constexpr uint8_t CHARGE_PIN = 5;
constexpr uint8_t SENSE_PIN = 6;

Feather feather;
CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN);
Stopwatch sw;
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
Format rateFormat("#####/s", Format::Alignment::RIGHT);

void setup()
{
   SerialX::begin(115200, 1000);
   feather.begin();

   sensor.begin();
}

float readSensor()
{
   return (float)sensor.chargeTimeMicros();
}

void loop()
{
   sw.reset();
   float value = readSensor();
   float elapsedMs = sw.elapsedMillis();
   uint16_t samplesPerSecond = elapsedMs > 0 ? (uint16_t)round(1000.0f / elapsedMs) : 0;

   feather.setCursor(0, 0);

   sampleCount++;
   for (uint8_t i = 0; i < NUM_TESTS; i++)
   {
      tests[i].set(value);
   }

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
