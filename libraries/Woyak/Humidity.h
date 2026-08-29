#pragma once

#include <math.h>

#include "Units.h"

///
/// <summary>
/// Derives humidity-related values (dew point, absolute humidity, heat index) from
/// a temperature and relative humidity reading. These are pure math functions with
/// no dependency on any particular sensor.
/// </summary>
///
namespace Humidity
{
   ///
   /// <summary>
   /// Computes the dew point using the Magnus formula.
   /// </summary>
   /// <param name="tempC">Air temperature in Celsius</param>
   /// <param name="humidity">Relative humidity, 0-100%</param>
   /// <returns>Dew point in Celsius</returns>
   ///
   inline float dewPointC(float tempC, float humidity)
   {
      constexpr float a = 17.62f;
      constexpr float b = 243.12f;

      float alpha = log(humidity / 100.0f) + (a * tempC) / (b + tempC);
      return (b * alpha) / (a - alpha);
   }

   ///
   /// <summary>
   /// Computes the dew point using the Magnus formula.
   /// </summary>
   /// <param name="tempF">Air temperature in Fahrenheit</param>
   /// <param name="humidity">Relative humidity, 0-100%</param>
   /// <returns>Dew point in Fahrenheit</returns>
   ///
   inline float dewPointF(float tempF, float humidity)
   {
      return Units::C2F(dewPointC(Units::F2C(tempF), humidity));
   }

   ///
   /// <summary>
   /// Computes the absolute humidity, i.e. the mass of water vapor per volume of air.
   /// </summary>
   /// <param name="tempC">Air temperature in Celsius</param>
   /// <param name="humidity">Relative humidity, 0-100%</param>
   /// <returns>Absolute humidity in grams per cubic meter</returns>
   ///
   inline float absoluteHumidity(float tempC, float humidity)
   {
      float saturationVaporPressure = 6.112f * exp((17.67f * tempC) / (tempC + 243.5f));
      return (saturationVaporPressure * humidity * 2.1674f) / (273.15f + tempC);
   }

   ///
   /// <summary>
   /// Computes the heat index (apparent temperature) using the NWS Rothfusz regression.
   /// </summary>
   /// <param name="tempF">Air temperature in Fahrenheit</param>
   /// <param name="humidity">Relative humidity, 0-100%</param>
   /// <returns>
   /// Heat index in Fahrenheit. Below 80F the heat index isn't well defined, so the
   /// input temperature is returned unchanged.
   /// </returns>
   ///
   inline float heatIndexF(float tempF, float humidity)
   {
      if (tempF < 80.0f)
      {
         return tempF;
      }

      float t = tempF;
      float rh = humidity;

      float hi = -42.379f
         + 2.04901523f * t
         + 10.14333127f * rh
         - 0.22475541f * t * rh
         - 0.00683783f * t * t
         - 0.05481717f * rh * rh
         + 0.00122874f * t * t * rh
         + 0.00085282f * t * rh * rh
         - 0.00000199f * t * t * rh * rh;

      if (rh < 13.0f && t >= 80.0f && t <= 112.0f)
      {
         hi -= ((13.0f - rh) / 4.0f) * sqrt((17.0f - fabs(t - 95.0f)) / 17.0f);
      }
      else if (rh > 85.0f && t >= 80.0f && t <= 87.0f)
      {
         hi += ((rh - 85.0f) / 10.0f) * ((87.0f - t) / 5.0f);
      }

      return hi;
   }
};
