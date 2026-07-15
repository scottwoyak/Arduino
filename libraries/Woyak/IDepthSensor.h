#pragma once

/// <summary>
/// Common interface for sensors that report a water depth measurement, regardless of the
/// underlying sensing technology (e.g. pressure-based or capacitive).
/// </summary>
class IDepthSensor
{
public:
   virtual ~IDepthSensor() = default;

   /// <summary>
   /// Initializes the underlying sensor hardware.
   /// </summary>
   /// <returns>True if the sensor initialized successfully; otherwise false.</returns>
   virtual bool begin() = 0;

   /// <summary>
   /// Gets the latest computed depth from the sensor, in whatever unit the concrete
   /// implementation is configured to report (see the implementation's documentation).
   /// </summary>
   /// <returns>The current depth reading.</returns>
   virtual float getDepth() = 0;
};
