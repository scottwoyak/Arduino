#pragma once

#include <cstdint>

#include "ColorX.h"

///
/// <summary>
/// Shared air quality tier thresholds, names and colors (Good through Hazardous),
/// following the EPA AQI categories, so Air_Viewer.ino and Air_Monitor_Display.ino (and
/// any other sketch rating the same measurements) can't drift apart. PM1.0 has no
/// standard of its own, so it shares PM2.5's limits.
/// </summary>
///
class AirQualityTiers
{
public:
   /// <summary>Number of air quality tiers, from best (index 0) to worst (index NUM_TIERS - 1).</summary>
   static constexpr uint8_t NUM_TIERS = 6;

   /// <summary>Tier names, shortened to fit under a narrow display column/bar.</summary>
   static constexpr const char* TIER_NAMES[NUM_TIERS] = { "Good", "Mod", "Sens", "Bad", "V.Bad", "Haz" };

   /// <summary>Dimmed tier colors (so a white value marker/text stands out on top of them).</summary>
   static constexpr Color TIER_COLORS[NUM_TIERS] = {
      Color565::fromRGB(0, 120, 0),
      Color565::fromRGB(153, 120, 0),
      Color565::fromRGB(153, 76, 0),
      Color565::fromRGB(153, 0, 0),
      Color565::fromRGB(86, 38, 91),
      Color565::fromRGB(96, 0, 30),
   };

   /// <summary>Upper limit of each tier except the last, for PM1.0 and PM2.5 (ug/m3, EPA AQI breakpoints).</summary>
   static constexpr float PM25_LIMITS[NUM_TIERS - 1] = { 9, 35, 55, 125, 225 };

   /// <summary>Upper limit of each tier except the last, for PM10 (ug/m3, EPA AQI breakpoints).</summary>
   static constexpr float PM10_LIMITS[NUM_TIERS - 1] = { 54, 154, 254, 354, 424 };

   /// <summary>Upper limit of each tier except the last, for CO2 (ppm).</summary>
   static constexpr float CO2_LIMITS[NUM_TIERS - 1] = { 800, 1000, 1500, 2000, 5000 };

   /// <summary>Upper limit of each tier except the last, for VOC (gas index value).</summary>
   static constexpr float VOC_LIMITS[NUM_TIERS - 1] = { 150, 250, 350, 400, 450 };

   /// <summary>Upper limit of each tier except the last, for NOx (gas index value).</summary>
   static constexpr float NOX_LIMITS[NUM_TIERS - 1] = { 20, 100, 200, 300, 400 };

   ///
   /// <summary>
   /// Finds the tier a value falls into, given its ascending per-tier upper limits.
   /// </summary>
   /// <param name="value">The value to rate.</param>
   /// <param name="limits">Upper limit of each tier except the last (NUM_TIERS - 1 entries).</param>
   /// <returns>Tier index, from 0 (best) to NUM_TIERS - 1 (worst).</returns>
   ///
   static uint8_t tierFor(float value, const float* limits)
   {
      uint8_t tier = 0;
      while (tier < NUM_TIERS - 1 && value > limits[tier])
      {
         tier++;
      }
      return tier;
   }

   ///
   /// <summary>
   /// Computes a continuous fractional tier score in [0.0, NUM_TIERS - 1] by linearly
   /// interpolating the value within the tier's lower and upper limits. Values at or
   /// below the first tier's upper limit map to [0.0, 1.0], each subsequent bounded tier
   /// maps to [tier, tier + 1.0], and values in the final (unbounded) tier return
   /// NUM_TIERS - 1 (5.0).
   /// </summary>
   /// <param name="value">The value to rate.</param>
   /// <param name="limits">Upper limit of each tier except the last (NUM_TIERS - 1 entries).</param>
   /// <returns>Continuous tier score in [0.0, NUM_TIERS - 1].</returns>
   ///
   static float scoreFor(float value, const float* limits)
   {
      if (value <= 0.0f)
      {
         return 0.0f;
      }

      uint8_t tier = tierFor(value, limits);
      if (tier >= NUM_TIERS - 1)
      {
         return static_cast<float>(NUM_TIERS - 1);
      }

      float lower = (tier == 0) ? 0.0f : limits[tier - 1];
      float upper = limits[tier];
      if (upper <= lower)
      {
         return static_cast<float>(tier);
      }

      return tier + (value - lower) / (upper - lower);
   }

   ///
   /// <summary>
   /// Picks a tier's color for a value, given its ascending per-tier upper limits.
   /// Returns Color::VALUE for NAN, so an unavailable reading doesn't appear rated.
   /// </summary>
   /// <param name="value">The value to rate.</param>
   /// <param name="limits">Upper limit of each tier except the last (NUM_TIERS - 1 entries).</param>
   /// <returns>The rating color.</returns>
   ///
   static Color colorFor(float value, const float* limits)
   {
      if (isnan(value))
      {
         return Color::VALUE;
      }
      return TIER_COLORS[tierFor(value, limits)];
   }
};
