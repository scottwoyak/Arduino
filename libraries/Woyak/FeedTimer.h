#pragma once

#include <NTPClient.h>

//
// Tracks when it is time to upload feed data back to the cloud
//
class FeedTimer {
private:
   NTPClient* _clock;
   unsigned long _millisAtNextSave;
   unsigned long _intervalSecs;
   unsigned long _offsetSecs;
   bool _first = true;

   void _syncWithClock() {
      //  
      // figure out the next save event time such that it is
      // an even multiple, e.g. if we save every 10 minutes,
      // we're saving at 9:00, 9:10, etc...
      //

      // get the current time of day in total seconds
      int hours = this->_clock->getHours();
      int mins = this->_clock->getMinutes();
      int secs = this->_clock->getSeconds();
      long currentSecs = 60 * 60 * hours + 60 * mins + secs;

      // figure out the next round time value... the first one
      // greater than the current time
      long desiredSecs = 0;
      while (desiredSecs <= currentSecs) {
         desiredSecs += this->_intervalSecs;
      }
      desiredSecs += this->_offsetSecs;

      // figure out the millis when we need to save again
      long deltaMillis = 1000 * (desiredSecs - currentSecs);
      this->_millisAtNextSave = millis() + deltaMillis;
   }

public:
   //
   // @param clock the internet clock
   // @param intervalSecs the time between uploads, e.g. upload every 10 secs.
   // Uploads will occur at times even with a real clock, e.g. if you request
   // every minute, it will happen on the minute, not some random second value
   // @param uploadImmediately if true, a value will be uploaded right now and
   // the next occurs at the appropriate time
   // @param offsetSecs the offset for when to upload. 0 is on the hour.
   //
   FeedTimer(
      NTPClient* clock,
      unsigned long intervalSecs,
      bool uploadImmediately = true,
      unsigned long offsetSecs = 0
   )
   {
      this->_clock = clock;
      this->_intervalSecs = intervalSecs;
      this->_first = uploadImmediately;
      this->_offsetSecs = offsetSecs;
   }

   //
   // Sets things up
   //
   void begin() {
      this->_syncWithClock();
   }

   //
   // Returns true when it is time to do an upload. Thereafter it will not
   // return true again until the interval amount of time has passed
   //
   bool ready() {
      if (this->_first) {
         this->_first = false;
         return true;
      }
      else {
         if (millis() > this->_millisAtNextSave) {
            this->_syncWithClock();
            return true;
         }
         else {
            return false;
         }
      }
   }

   unsigned long msUntilNextSave() {
      unsigned long now = millis();
      if (this->_millisAtNextSave > now) {
         return this->_millisAtNextSave - now;
      }
      else {
         return 0;
      }
   }
};
