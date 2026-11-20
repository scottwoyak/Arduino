#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "PowerSensorBase.h"

///
/// <summary>
/// Shared CT and board phase calibration constants produced by the CT_Calibration sketch
/// and consumed by MuxAdcPowerSensor and Power_Publisher.
///
/// After a calibration capture:
/// 1. Update DEFAULT_PHASE_SHIFT_DEG from the Reference CT (*) line in Serial output.
/// 2. Update existing CT_TYPE_TABLE entries in place, or append new CT types at the end
///    (and increment NUM_CT_TYPES) so saved NVS preset indices on deployed boards stay valid.
/// </summary>
///
namespace CTCalibration
{
   ///
   /// <summary>
   /// Editable CT type preset (display label, rated amps, A/V gain, and phase offset
   /// relative to the reference CT).
   /// </summary>
   ///
   struct CtTypePreset
   {
      const char* displayName;
      uint16_t ratedAmps;
      float ampsPerVolt;
      float phaseOffsetDeg;
   };



   // Nominal 9V AC wall-wart + ZMPT101B + reference CT phase lead at 60 Hz (degrees).
   inline constexpr float DEFAULT_PHASE_SHIFT_DEG = 31.0f;

   // Editable CT type presets. Append new calibrated CTs at the end to preserve saved NVS indices.
   inline constexpr size_t NUM_CT_TYPES = 5;
   inline constexpr std::array<CtTypePreset, NUM_CT_TYPES> CT_TYPE_TABLE = { {
      { "20SC",   20, 40.01,  0.0f },  // 20A SCT with 20 ohm burden
      { "30SC",   30,  PowerSensorBase::CORE_30A,  0.0f },  // 30A SCT
      { "100R",  100,  99.8f,    0.00f },                   // 100A Round CT with 20 ohm burden
      { "100SC", 100,  99.8f,    0.00f },                   // 100A SCT with 30 ohm burden
      { "500R",  500, PowerSensorBase::CORE_213A, 0.0f },  // 500A:50mA + 47 ohm burden
   } };
}
