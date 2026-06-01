#include <deque>
#include <Util.h>
#include <Stopwatch.h>

class BinData
{
public:
   unsigned long micros;
   float value;

   BinData(float v)
   {
	  value = v;
	  micros = ::micros();
   }
};

class TimedBin2
{
private:
   std::deque<BinData*> _data;
   unsigned long _spanMicros;

   void _advance()
   {
	  unsigned long now = micros();

	  while (_data.empty() == false && Util::getSpan(_data.back()->micros, now) > _spanMicros)
	  {
		 BinData* item = _data.back();
		 _data.pop_back();
		 delete item;
	  }
   }

public:
   TimedBin2(unsigned long spanMicros)
   {
	  _spanMicros = spanMicros;
   }

   void log(float value)
   {
	  _advance();

	  _data.push_front(new BinData(value));
   }

   std::size_t count()
   {
	  return _data.size();
   }

   float getAverage()
   {
	  float total = 0;
	  for (int i = 0; i < _data.size(); i++)
	  {
		 total += _data[i]->value;
	  }

	  return total / _data.size();

   }
};

class MemoryProfiler
{
private:
   uint32_t _freeHeapAtStart;
   uint32_t _lastFreeHeap;

public:
   MemoryProfiler()
   {
	  _freeHeapAtStart = ESP.getFreeHeap();
	  _lastFreeHeap = _freeHeapAtStart;
   }

   void tag(const char* msg)
   {
	  uint32_t nowFreeHeap = ESP.getFreeHeap();
	  int32_t delta = (int32_t) (_lastFreeHeap - nowFreeHeap);
	  /*
	  Serial.print(String(ESP.getFreeHeap()));
	  Serial.print("-");
	  Serial.print(String(_freeHeapAtStart));
	  Serial.print("=");
	  Serial.println(String(ESP.getFreeHeap() - _freeHeapAtStart));
	  */

	  Serial.print(msg);
	  Serial.print(" ");
	  if (delta > 0) Serial.print("+");
	  Serial.print(String(delta));
	  Serial.println(" bytes");

	  _lastFreeHeap = nowFreeHeap;
   }

   int32_t deltaFromStart()
   {
	  return (int32_t)(_freeHeapAtStart - ESP.getFreeHeap());
   }

   uint32_t getFreeHeap()
   {
	  return ESP.getFreeHeap();
   }
};

TimedBin2 bin(30000);
MemoryProfiler mp;

void setup()
{
   Serial.begin(115200);
   while (!Serial);

   setCpuFrequencyMhz(80);
}

void test()
{
   /*
   Serial.printf("Total Heap Size:   %d bytes\n", ESP.getHeapSize());
   Serial.printf("Free Heap Available: %d bytes\n", ESP.getFreeHeap());
   Serial.printf("Lowest Free Heap:    %d bytes\n", ESP.getMinFreeHeap());
   Serial.printf("Max Allocatable Block: %d bytes\n", ESP.getMaxAllocHeap());
   Serial.println("-------------------------------------");
   */

   Stopwatch sw;

   constexpr auto NUM_ITEMS = 10000;
   for (int i = 0; i < 10000; i++)
   {
	  bin.log(random());
   }
   mp.tag("bin loaded");

   ulong elapsedMicros = sw.elapsedMicros();
   double elapsedMillis = sw.elapsedMillis();
   Serial.println("Total Delta: " + String(mp.deltaFromStart()));
   Serial.println("Elapsed: " + String(elapsedMicros) + " micros");
   Serial.println("Count: " + String(bin.count()));
   Serial.println(String(elapsedMillis/NUM_ITEMS,4) + " ms per item");
   Serial.println("Remaining: " + String(mp.getFreeHeap()) + " bytes");
   //Serial.printf("Lowest Free Heap:    %d bytes\n", ESP.getMinFreeHeap());
   //Serial.printf("Max Allocatable Block: %d bytes\n", ESP.getMaxAllocHeap());


}

void loop()
{
   for (int i = 0; i < 10; i++)
   {
	  test();
	  Serial.println();
   }

   Stopwatch sw;
   float avg = bin.getAverage();
   float elapsedMillis = sw.elapsedMillis();
   Serial.println("Average: " + String(avg) + " " + String(elapsedMillis));
   delay(2000);
}
