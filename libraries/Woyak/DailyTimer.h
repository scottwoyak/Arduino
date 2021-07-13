#pragma once

#include <NTPClient.h>

class DailyTimer {
  NTPClient *_clock;
  int _day;

  DailyTimer(NTPClient* clock) { 
     this->_clock = clock;
     this->_day = clock->getDay();
  }

  bool ready() {
    if (this->_clock->getDay() != this->_day) {
        this->_day = this->_clock->getDay(); 
        return true; 
        }
        else {
          return false;
        }
  }
};
