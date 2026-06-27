#pragma once

namespace Units
{
   inline float C2F(float celsius)
   {
      return 32.0f + (9.0f / 5.0f) * celsius;
   }

   inline float F2C(float fahrenheit)
   {
      return (fahrenheit - 32.0f) * (5.0f / 9.0f);
   }

   inline float M2FT(float meters)
   {
      return meters * 3.28084f;
   }

   inline float FT2M(float feet)
   {
      return feet / 3.28084f;
   }

   inline float M2IN(float meters)
   {
      return meters * 39.3701f;
   }

   inline float IN2M(float inches)
   {
      return inches / 39.3701f;
   }
};
