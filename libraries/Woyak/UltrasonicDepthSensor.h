#pragma once

#include <Arduino.h>
#include <cmath>

#include "DepthSensorBase.h"

///
/// <summary>
/// Measures depth using an ultrasonic (HC-SR04 style) sensor. The trigger pin emits a
/// pulse and the echo pin's high time is converted into a distance.
/// </summary>
///
class UltrasonicDepthSensor : public DepthSensorBase
{
private:
   uint8_t _triggerPin;
   uint8_t _echoPin;
   uint16_t _echoTimeoutMicros;

public:
   static constexpr uint16_t DEFAULT_ECHO_TIMEOUT_MICROS = 30000;

   ///
   /// <summary>
   /// Initializes a depth sensor wrapper around an ultrasonic distance sensor.
   /// </summary>
   /// <param name="triggerPin">GPIO pin used to trigger the ultrasonic pulse.</param>
   /// <param name="echoPin">GPIO pin used to measure the echo pulse width.</param>
   /// <param name="echoTimeoutMicros">Maximum time to wait for the echo pulse before giving up.</param>
   ///
   UltrasonicDepthSensor(
      uint8_t triggerPin,
      uint8_t echoPin,
      uint16_t echoTimeoutMicros = DEFAULT_ECHO_TIMEOUT_MICROS)
   {
      _triggerPin = triggerPin;
      _echoPin = echoPin;
      _echoTimeoutMicros = echoTimeoutMicros;
   }

   ///
   /// <summary>
   /// Configures the trigger and echo pins.
   /// </summary>
   /// <returns>Always true; the ultrasonic sensor has no failure mode to report.</returns>
   ///
   bool begin() override
   {
      pinMode(_triggerPin, OUTPUT);
      pinMode(_echoPin, INPUT);
      return true;
   }

   protected:
   ///
   /// <summary>
   /// Triggers a new measurement and computes the resulting distance, in centimeters.
   /// </summary>
   /// <returns>The measured distance in centimeters, or NAN if the echo pulse timed out.</returns>
   ///
   float readRawDepth() override
   {
      digitalWrite(_triggerPin, LOW);
      delayMicroseconds(2);

      digitalWrite(_triggerPin, HIGH);
      delayMicroseconds(10);
      digitalWrite(_triggerPin, LOW);

      long durationMicros = pulseIn(_echoPin, HIGH, _echoTimeoutMicros);
      if (durationMicros == 0)
      {
         return NAN;
      }

      float distanceCM = durationMicros * 0.034f / 2;
      return distanceCM;
   }
};
