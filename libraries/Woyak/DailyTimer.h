#pragma once

#include <NTPClient.h>

class DailyTimer {
private:
   NTPClient* _clock;
   int _lastSavedDay;

   int _getDay() {
      return this->_clock->getEpochTime() / (24 * 60 * 60);
   }

public:
   DailyTimer(NTPClient* clock) {
      this->_clock = clock;
   }

   void begin() {
      this->_lastSavedDay = this->_getDay();
   }

   bool ready() {
      int today = this->_getDay();
      if (today != this->_lastSavedDay) {
         this->_lastSavedDay = today;
         return true;
      }
      else {
         return false;
      }
   }
};
