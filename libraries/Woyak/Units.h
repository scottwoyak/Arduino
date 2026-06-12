#pragma once

namespace Units
{
   float C2F(float celsius)
   {
      return 32 + (9.0 / 5.0) * celsius;
   }

   float F2C(float fahrenheit)
   {
      return (fahrenheit - 32) * (5.0 / 9.0);
   }

   float M2FT(float meters)
   {
      return meters * 3.28084;
   }

   float FT2M(float feet)
   {
      return feet / 3.28084;
   }

   float M2IN(float meters)
   {
      return meters * 39.3701;
   }

   float IN2M(float inches)
   {
      return inches / 39.3701;
   }
};