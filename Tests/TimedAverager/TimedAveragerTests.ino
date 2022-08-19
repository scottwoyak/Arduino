
#include <AUnit.h>
#include <TimedAverager.h>

class TimedValue {
public:
   unsigned long millis;
   float value;
};

// mock timing for Stopwatch
unsigned long ticks;
unsigned long getTicks() {
   return ticks;
}

void loadValues(
   TimedAverager* averager,
   TimedValue values[],
   uint numValues
) {
   for (int i = 0; i < numValues; i++) {
      ticks = 1000 * values[i].millis;
      averager->set(values[i].value);
   }
}

test(shouldComputeAverages001) {

   uint numValues = 5;
   TimedValue values[] = {
      { 5, 1.0},
      { 15, 2.0},
      { 25, 3.0},
      { 35, 4.0},
      { 45, 5.0},
   };

   ticks = 0;
   uint numBuckets = 5;
   uint msPerBucket = 10;
   uint totalTime = msPerBucket * numBuckets;
   TimedAverager averager(totalTime, numBuckets);

   loadValues(&averager, values, numValues);

   ticks = 50 * 1000;
   assertEqual(3.0, averager.get());
}

test(shouldComputeAverages002) {

   uint numValues = 3;
   TimedValue values[] = {
      { 5, 1.0},
      { 15, 2.0},
      { 25, 3.0},
   };

   ticks = 0;
   uint numBuckets = 3;
   uint msPerBucket = 10;
   uint totalTime = msPerBucket * numBuckets;
   TimedAverager averager(totalTime, numBuckets);

   loadValues(&averager, values, numValues);

   ticks = 30 * 1000;
   assertEqual(2.0, averager.get());
}

test(shouldComputeAverages003) {

   uint numValues = 1;
   TimedValue values[] = {
      { 5, 1.0},
   };

   ticks = 0;
   uint numBuckets = 3;
   uint msPerBucket = 10;
   uint totalTime = msPerBucket * numBuckets;
   TimedAverager averager(totalTime, numBuckets);

   loadValues(&averager, values, numValues);

   ticks = 30 * 1000;
   assertEqual(1.0, averager.get());
}

test(shouldComputeAverages004) {

   uint numValues = 2;
   TimedValue values[] = {
      { 5, 1.0},
      { 15, 2.0},
   };


   ticks = 0;
   uint numBuckets = 3;
   uint msPerBucket = 10;
   uint totalTime = msPerBucket * numBuckets;
   TimedAverager averager(totalTime, numBuckets);

   loadValues(&averager, values, numValues);

   ticks = 30 * 1000;
   assertEqual(1.5, averager.get());
}

test(shouldComputeAverages005) {
   uint numValues = 4;
   TimedValue values[] = {
      { 5, 1.0},
      { 15, 2.0},
      { 25, 3.0},
      { 35, 4.0},
   };

   ticks = 0;
   uint numBuckets = 3;
   uint msPerBucket = 10;
   uint totalTime = msPerBucket * numBuckets;
   TimedAverager averager(totalTime, numBuckets);

   loadValues(&averager, values, numValues);

   ticks = 35 * 1000;
   assertEqual(2.5, averager.get());
}

test(shouldComputeAverages006) {

   ticks = 0;
   uint numBuckets = 3;
   uint msPerBucket = 10;
   uint totalTime = msPerBucket * numBuckets;
   TimedAverager averager(totalTime, numBuckets);

   for (int i = 0; i < 40; i++) {
      ticks = 1000 * 2 * i;
      averager.set(1);
      ticks += 1000;
      assertEqual(averager.get(), 1.0);
   }
}

/*
test(shouldComputeAverages007) {

   ticks = 0;
   uint numBuckets = 3;
   uint msPerBucket = 10;
   uint totalTime = msPerBucket * numBuckets;
   TimedAverager averager(totalTime, numBuckets);

   float last = 0;
   for (int i = 0; i < 50; i++) {
      ticks = 1000 * 2 * i;
      averager.set(i);
      //ticks += 1000;

      Serial.print("Time: ");
      Serial.print(ticks / 1000);
      Serial.print(" Value: ");
      Serial.print(averager.get());
      Serial.print(" ");
      Serial.print(averager.get() - last);
      last = averager.get();
      Serial.println();
      //assertEqual(averager.get(), 1.0);
   }
}
*/

void setup() {
   Serial.begin(115200);
   while (!Serial);

   Stopwatch::tickFunc = getTicks;
}

void loop() {
   aunit::TestRunner::run();
}
