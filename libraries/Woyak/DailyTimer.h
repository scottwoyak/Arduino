#pragma once

#include <NTPClient.h>

class DailyTimer
{
private:
   NTPClient* _clock;
   int _lastSavedDay;

   int _getDay() const
   {
      return _clock->getEpochTime() / (24 * 60 * 60);
   }

public:
   explicit DailyTimer(NTPClient* clock)
   {
      _clock = clock;
   }

   void begin()
   {
      _lastSavedDay = _getDay();
   }

   bool ready()
   {
      int today = _getDay();
      if (today != _lastSavedDay)
      {
         _lastSavedDay = today;
         return true;
      }

      return false;
   }
};
