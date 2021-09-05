#pragma once

#include <AccumulatingAverager.h>
#include <Util.h>

//
// Class for measuring battery state
//
class Battery {
private:
   uint8_t _batteryVoltsPin;
   uint8_t _batteryChargingVoltsPin;
   AccumulatingAverager _batteryVolts;
   AccumulatingAverager _chargingVolts;
   float _lastVolts;
   uint8_t _consecutiveUnchangedVolts;
   float _fullChargeVolts;

public:
   Battery(uint8_t batteryVoltsPin, uint8_t batteryChargingVoltsPin) :
      _batteryVolts(1, 5), _chargingVolts(1, 5) {
      this->_batteryVoltsPin = batteryVoltsPin;
      this->_batteryChargingVoltsPin = batteryChargingVoltsPin;
      this->_lastVolts = 0;
      this->_consecutiveUnchangedVolts = 0;
   }

   //
   // Initializes pins as input pins and sets analog read resolution to 12
   //
   void begin() {
      pinMode(this->_batteryVoltsPin, INPUT);
      pinMode(this->_batteryChargingVoltsPin, INPUT);
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
      this->_chargingVolts.set(Util::readVolts(this->_batteryChargingVoltsPin));
   }

   //
   // Once a measurement time has elapsed, call this function to reset the
   // averaging functions
   //
   void reset() {
      // try to detect when we're fully charged and record the volts so we
      // can properly compute 100% charge
      if (abs(this->_lastVolts - this->_batteryVolts.get()) < 0.005) {
         this->_consecutiveUnchangedVolts++;

         if (this->_consecutiveUnchangedVolts >= 4 && this->_batteryVolts.get() > 4) {
            this->_fullChargeVolts = this->_batteryVolts.get();
         }
      }
      else {
         this->_lastVolts = this->_batteryVolts.get();
         this->_consecutiveUnchangedVolts = 0;
      }

      this->_chargingVolts.reset();
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
   // Gets the volts from the charger
   //
   float getChargingVolts() {
      return this->_chargingVolts.get();
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