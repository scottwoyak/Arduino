#pragma once

#include <AccumulatingAverager.h>
#include <Stopwatch.h>
#include <ValueStore.h>
#include <limits>
#include <algorithm>

class TimedAverager : public IValueStore {
private:
   AccumulatingAverager** _buckets;
   uint _numBuckets;
   uint _currentBucket = 0;
   ulong _bucketMs;
   Stopwatch _sw;

   float _advance() {
      // if enough time has passed, switch to the next bucket
      float elapsed = this->_sw.elapsedMillis();
      while (elapsed > this->_bucketMs) {
         this->_currentBucket++;
         if (this->_currentBucket >= this->_numBuckets) {
            this->_currentBucket = 0;
         }

         this->_buckets[this->_currentBucket]->reset();
         elapsed -= this->_bucketMs;
         this->_sw.reset(elapsed);
      }

      return elapsed;
   }

public:
   TimedAverager(ulong durationMs,
      uint nBuckets = 10,
      float minRange = -__FLT_MAX__,
      float maxRange = __FLT_MAX__) {

      this->_numBuckets = std::max(nBuckets, (uint)1) + 1;

      this->_buckets = new AccumulatingAverager * [this->_numBuckets];
      for (int i = 0; i < this->_numBuckets; i++) {
         this->_buckets[i] = new AccumulatingAverager(minRange, maxRange);
      }
      this->_bucketMs = std::max(1.0f, ((float)durationMs) / nBuckets);
   }

   ~TimedAverager() {
      for (int i = 0; i < this->_numBuckets; i++) {
         delete this->_buckets[i];
      }
      delete this->_buckets;
   }

   boolean set(float value) {
      this->_advance();

      /*
      Serial.print(" storing ");
      Serial.print(value);
      Serial.print(" in bucket ");
      Serial.print(this->_currentBucket);
      Serial.print(" ");
      Serial.print(this->_sw.elapsedMillis());
      Serial.println();
*/

      return this->_buckets[this->_currentBucket]->set(value);
   }

   float get() {
      float elapsed = this->_advance();

      float total = 0;
      float count = 0;

      uint firstBucket = this->_currentBucket + 1;
      if (firstBucket >= this->_numBuckets) {
         firstBucket = 0;
      }

      for (uint i = 0; i < this->_numBuckets; i++) {

         if (this->_buckets[i]->getCount() == 0) {
            continue;
         }

         float fraction = 1;
         if (i == this->_currentBucket) {
            fraction = elapsed / this->_bucketMs;
         }
         else if (i == firstBucket) {
            fraction = 1 - (elapsed / this->_bucketMs);
         }

         /*
         Serial.print("bucket: ");
         Serial.print(i);
         Serial.print(" value: ");
         Serial.print(this->_buckets[i]->get());
         Serial.print(" fraction: ");
         Serial.print(fraction);
         Serial.println();
*/

         total += fraction * this->_buckets[i]->get() * this->_buckets[i]->getCount();
         count += fraction * this->_buckets[i]->getCount();
      }

      /*
      Serial.print("fraction=");
      Serial.print(fraction);
      Serial.print(" ");
      Serial.print(this->_sw.elapsedMillis());
      Serial.print(" ");
      Serial.print(this->_bucketMs);
      Serial.println();
      count += fraction;
      total += fraction * this->_buckets[this->_currentBucket]->get();
*/

/*
Serial.print("total: ");
Serial.print(total);
Serial.print(" count: ");
Serial.print(count);
Serial.println();
*/

      return total / count;
   }

   float getMin() {
      this->_advance();

      float value = __FLT_MAX__;
      for (uint i = 0; i < this->_numBuckets; i++) {
         value = min(value, this->_buckets[i]->getMin());
      }

      return value;
   }

   float getMax() {
      this->_advance();

      float value = -__FLT_MAX__;
      for (uint i = 0; i < this->_numBuckets; i++) {
         value = max(value, this->_buckets[i]->getMax());
      }

      return value;
   }

   void reset() {
      for (uint i = 0; i < this->_numBuckets; i++) {
         this->_buckets[i]->reset();
      }
   }

   unsigned long getCount() {
      this->_advance();

      ulong count = 0;
      for (uint i = 0; i < this->_numBuckets; i++) {
         count += this->_buckets[i]->getCount();
      }

      return count;
   }

   unsigned long getBadCount() {
      this->_advance();

      ulong count = 0;
      for (uint i = 0; i < this->_numBuckets; i++) {
         count += this->_buckets[i]->getBadCount();
      }

      return count;
   }

   uint numBuckets() {
      return this->_numBuckets;
   }

   uint currentBucket() {
      return this->_currentBucket;
   }
};