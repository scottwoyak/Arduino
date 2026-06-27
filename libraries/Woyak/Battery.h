#pragma once

#include <cmath>

#include "RollingStats.h"
#include "Util.h"

/// <summary>
/// Monitors battery voltage and estimates charge percentage.
/// </summary>
/// <remarks>
/// Tracks battery voltage using a rolling statistics filter for stable readings.
/// Can optionally estimate and calibrate the fully-charged voltage level for accurate
/// percentage calculations. Call read() frequently (e.g., in loop) for continuous monitoring.
/// </remarks>
class Battery
{
private:
   uint8_t _batteryVoltsPin;
   RollingStats _batteryVolts;
   float _lastVolts;
   uint8_t _consecutiveUnchangedVolts;
   float _fullChargeVolts;

public:
   /// <summary>
   /// Constructs a Battery monitor on the specified analog pin.
   /// </summary>
   /// <param name="batteryVoltsPin">Analog pin for reading battery voltage (default pin 7)</param>
   Battery(uint8_t batteryVoltsPin = 7)
      : _batteryVoltsPin(batteryVoltsPin),
      _batteryVolts(1, 5),
      _lastVolts(0),
      _consecutiveUnchangedVolts(0),
      _fullChargeVolts(NAN)
   {
   }

   /// <summary>
   /// Initializes the battery monitor and sets analog read resolution.
   /// </summary>
   /// <remarks>
   /// Performs an initial voltage read. Call once during setup().
   /// </remarks>
   void begin()
   {
      analogReadResolution(12);

      // do an initial read
      read();
   }

   /// <summary>
   /// Reads the current battery voltage. Call frequently (e.g., in loop).
   /// </summary>
   void read()
   {
      _batteryVolts.set(Util::readVolts(_batteryVoltsPin));
   }

   /// <summary>
   /// Updates the full-charge voltage estimate based on voltage stability.
   /// </summary>
   /// <remarks>
   /// Automatically detects when battery voltage stabilizes and stores the stable voltage
   /// as the reference for 100% charge. This improves percentage accuracy over time.
   /// </remarks>
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

   /// <summary>
   /// Gets the voltage stored as the fully-charged reference.
   /// </summary>
   /// <returns>Full charge voltage in volts, or NaN if not yet calibrated</returns>
   float getFullChargeVolts()
   {
      return _fullChargeVolts;
   }

   /// <summary>
   /// Gets the current smoothed battery voltage.
   /// </summary>
   /// <returns>Battery voltage in volts</returns>
   float getBatteryVolts()
   {
      return _batteryVolts.get();
   }

   /// <summary>
   /// Gets the estimated battery charge percentage.
   /// </summary>
   /// <returns>Charge percentage from 0 to 100. Uses calibrated full-charge voltage if available.</returns>
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
