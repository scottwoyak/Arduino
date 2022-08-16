#pragma once

#include <AccumulatingAverager.h>
#include <Util.h>

//
// Class for measuring battery state
//
class Battery {
private:
   uint8_t _batteryVoltsPin;
   AccumulatingAverager _batteryVolts;
   float _lastVolts;
   uint8_t _consecutiveUnchangedVolts;
   float _fullChargeVolts;

public:
   Battery(uint8_t batteryVoltsPin = 7) :
      _batteryVolts(1, 5) {
      this->_batteryVoltsPin = batteryVoltsPin;
      this->_lastVolts = 0;
      this->_consecutiveUnchangedVolts = 0;
   }

   //
   // Initializes pins as input pins and sets analog read resolution to 12
   //
   void begin() {
      analogReadResolution(12);

      // do an initial read
      this->read();
   }

   //
   // Reads the current volt values into the averagers. Call this as often
   // as possible (e.g. in loop() function)
   //
   void read() {
      this->_batteryVolts.set(Util::readVolts(this->_batteryVoltsPin));
   }

   //
   // Once a measurement time has elapsed, call this function to reset the
   // averaging functions
   //
   void reset() {
      // try to detect when we're fully charged and record the volts so we
      // can properly compute 100% charge
      if (abs(this->_lastVolts - this->_batteryVolts.get()) < 0.01) {
         this->_consecutiveUnchangedVolts++;

         if (this->_consecutiveUnchangedVolts >= 4 && this->_batteryVolts.get() > 4) {
            this->_fullChargeVolts = this->_batteryVolts.get();
         }
      }
      else {
         this->_lastVolts = this->_batteryVolts.get();
         this->_consecutiveUnchangedVolts = 0;
      }

      this->_batteryVolts.reset();
   }

   //
   // Gets the volts associated with a fully charged battery
   //
   float getFullChargeVolts() {
      return this->_fullChargeVolts;
   }

   //
   // Gets the current battery volts
   //
   float getBatteryVolts() {
      return this->_batteryVolts.get();
   }

   //
   // Gets the percent charge
   //
   float getPercent() {
      if (this->_fullChargeVolts > 0) {
         return Util::voltsToPercent(this->_batteryVolts.get(), this->_fullChargeVolts);
      }
      else {
         return Util::voltsToPercent(this->_batteryVolts.get());
      }
   }
};