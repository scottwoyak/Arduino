#pragma once

///
/// <summary>
/// Base class for Arduino platforms.
/// </summary>
///
class ArduinoBase
{
public:
   ///
   /// <summary>
   /// Initialize the Arduino platform.
   /// </summary>
   ///
   virtual void begin() = 0;
};
