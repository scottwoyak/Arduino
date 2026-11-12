#pragma once

///
/// <summary>
/// Canonical, case-sensitive ASCII unit symbols for telemetry topic metadata.
/// Values use the declared unit without implicit conversion. Viewers may render
/// display-friendly symbols such as degrees or micrograms independently.
/// Dimensionless indices, scores, and simulated values omit the unit attribute;
/// their descriptions identify the measurement instead.
/// </summary>
///
namespace TelemetryUnits
{
   inline constexpr const char* WATTS = "W";
   inline constexpr const char* AMPS = "A";
   inline constexpr const char* VOLTS = "V";
   inline constexpr const char* DEGREES = "deg";
   inline constexpr const char* FAHRENHEIT = "degF";
   inline constexpr const char* CELSIUS = "degC";
   inline constexpr const char* MILES_PER_HOUR = "mph";
   inline constexpr const char* CENTIMETERS = "cm";
   inline constexpr const char* MILLIMETERS = "mm";
   inline constexpr const char* LUX = "lx";
   inline constexpr const char* MICROGRAMS_PER_CUBIC_METER = "ug/m3";
   inline constexpr const char* PARTS_PER_MILLION = "ppm";
   inline constexpr const char* PERCENT = "%";
}
