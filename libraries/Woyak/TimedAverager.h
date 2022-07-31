#pragma once

#include <AccumulatingAverager.h>
#include <Stopwatch.h>
#include <ValueStore.h>
#include <limits>

class TimedAverager : public IValueStore {
private:
   AccumulatingAverager** _buckets;
   uint _numBuckets;
   uint _currentBucket = 0;
   ulong _bucketMs;
   Stopwatch _sw;

public:
   TimedAverager(ulong durationMs,
      uint nBuckets = 10,
      float minRange = -__FLT_MAX__,
      float maxRange = __FLT_MAX__) {

      this->_numBuckets = max(nBuckets, 1);

      this->_buckets = new AccumulatingAverager * [nBuckets];
      for (int i = 0; i < this->_numBuckets; i++) {
         this->_buckets[i] = new AccumulatingAverager(minRange, maxRange);
      }
      this->_bucketMs = max(1, durationMs / nBuckets);
   }

   ~TimedAverager() {
      for (int i = 0; i < this->_numBuckets; i++) {
         delete this->_buckets[i];
      }
      delete this->_buckets;
   }

   boolean set(float value) {
      // if enough time has passed, switch to the next bucket
      if (this->_sw.elapsedMillis() > this->_bucketMs) {
         // in a perfect world we advance to the correct bucket, but we're assuming
         // things are all moving pretty rapidly
         this->_currentBucket++;
         if (this->_currentBucket >= this->_numBuckets) {
            this->_currentBucket = 0;
         }

         this->_buckets[this->_currentBucket]->reset();
         this->_sw.reset();
      }

      return this->_buckets[this->_currentBucket]->set(value);
   }

   float get() {
      float total = 0;
      float count = 0;

      for (uint i = 0; i < this->_numBuckets; i++) {
         if (i == this->_currentBucket) {
            continue;
         }

         if (this->_buckets[i]->getCount() == 0) {
            continue;
         }

         total += this->_buckets[i]->get();
         count++;
      }

      float fraction = this->_sw.elapsedMillis() / this->_bucketMs;
      count += fraction;
      total += fraction * this->_buckets[this->_currentBucket]->get();

      return total / count;
   }

   float getMin() {
      float value = __FLT_MAX__;
      for (uint i = 0; i < this->_numBuckets; i++) {
         value = min(value, this->_buckets[i]->getMin());
      }

      return value;
   }

   float getMax() {
      float value = -__FLT_MAX__;
      for (uint i = 0; i < this->_numBuckets; i++) {
         value = max(value, this->_buckets[i]->getMin());
      }

      return value;
   }

   void reset() {
      for (uint i = 0; i < this->_numBuckets; i++) {
         this->_buckets[i]->reset();
      }
   }

   unsigned long getCount() {
      ulong count = 0;
      for (uint i = 0; i < this->_numBuckets; i++) {
         count += this->_buckets[i]->getCount();
      }

      return count;
   }

   unsigned long getBadCount() {
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