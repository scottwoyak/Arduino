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


   // Generated from CT_Calibration Sketch
   inline constexpr float DEFAULT_PHASE_SHIFT_DEG = 33.37f;  // Ch 9 (100R)
   inline constexpr size_t NUM_CT_TYPES = 5;
   inline constexpr std::array<CtTypePreset, NUM_CT_TYPES> CT_TYPE_TABLE = { {
      { "20SC",  20,   40.06f,  -2.15f },
      { "30SC",  30,   29.91f,  -1.66f },
      { "100R",  100,  99.92f,  +0.00f },
      { "100SC", 100,  98.91f,  -0.80f },
      { "100NH", 100, 102.88f,  +0.72f },
   } };
}
