#pragma once

/// <summary>
/// Unit conversion functions for temperature, distance, and other measurements.
/// </summary>
namespace Units
{
   /// <summary>Converts temperature from Celsius to Fahrenheit.</summary>
   /// <param name="celsius">Temperature in Celsius</param>
   /// <returns>Temperature in Fahrenheit</returns>
   inline float C2F(float celsius)
   {
      return 32.0f + (9.0f / 5.0f) * celsius;
   }

   /// <summary>Converts temperature from Fahrenheit to Celsius.</summary>
   /// <param name="fahrenheit">Temperature in Fahrenheit</param>
   /// <returns>Temperature in Celsius</returns>
   inline float F2C(float fahrenheit)
   {
      return (fahrenheit - 32.0f) * (5.0f / 9.0f);
   }

   /// <summary>Converts distance from meters to feet.</summary>
   /// <param name="meters">Distance in meters</param>
   /// <returns>Distance in feet</returns>
   inline float M2FT(float meters)
   {
      return meters * 3.28084f;
   }

   /// <summary>Converts distance from feet to meters.</summary>
   /// <param name="feet">Distance in feet</param>
   /// <returns>Distance in meters</returns>
   inline float FT2M(float feet)
   {
      return feet / 3.28084f;
   }

   /// <summary>Converts distance from meters to inches.</summary>
   /// <param name="meters">Distance in meters</param>
   /// <returns>Distance in inches</returns>
   inline float M2IN(float meters)
   {
      return meters * 39.3701f;
   }

   /// <summary>Converts distance from inches to meters.</summary>
   /// <param name="inches">Distance in inches</param>
   /// <returns>Distance in meters</returns>
   inline float IN2M(float inches)
   {
      return inches / 39.3701f;
   }
};
