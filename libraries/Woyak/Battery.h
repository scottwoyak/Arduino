#pragma once

#include <cmath>

#include "RollingStats.h"
#include "Util.h"

//
// Class for measuring battery state
//
class Battery
{
private:
   uint8_t _batteryVoltsPin;
   RollingStats _batteryVolts;
   float _lastVolts;
   uint8_t _consecutiveUnchangedVolts;
   float _fullChargeVolts;

public:
   Battery(uint8_t batteryVoltsPin = 7)
      : _batteryVoltsPin(batteryVoltsPin),
      _batteryVolts(1, 5),
      _lastVolts(0),
      _consecutiveUnchangedVolts(0),
      _fullChargeVolts(NAN)
   {
   }

   //
   // Initializes pins as input pins and sets analog read resolution to 12
   //
   void begin()
   {
      analogReadResolution(12);

      // do an initial read
      read();
   }

   //
   // Reads the current volt values into the averagers. Call this as often
   // as possible (e.g. in loop() function)
   //
   void read()
   {
      _batteryVolts.set(Util::readVolts(_batteryVoltsPin));
   }

   void updateFullChargeEstimate()
   {
      // try to detect when we're fully charged and record the volts so we
      // can properly compute 100% charge
      if (fabsf(_lastVolts - _batteryVolts.get()) < 0.01f)
      {
         _consecutiveUnchangedVolts++;

         if ((_consecutiveUnchangedVolts >= 4) && (_batteryVolts.get() > 4.0f))
         {
            _fullChargeVolts = _batteryVolts.get();
         }
      }
      else
      {
         _lastVolts = _batteryVolts.get();
         _consecutiveUnchangedVolts = 0;
      }
   }

   //
   // Gets the volts associated with a fully charged battery
   //
   float getFullChargeVolts()
   {
      return _fullChargeVolts;
   }

   //
   // Gets the current battery volts
   //
   float getBatteryVolts()
   {
      return _batteryVolts.get();
   }

   //
   // Gets the percent charge
   //
   float getPercent()
   {
      if (_fullChargeVolts > 0)
      {
         return Util::voltsToPercent(_batteryVolts.get(), _fullChargeVolts);
      }
      else
      {
         return Util::voltsToPercent(_batteryVolts.get());
      }
   }
};
