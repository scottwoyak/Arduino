#pragma once

#include <span>
#include <stdint.h>

///
/// <summary>
/// Publisher's telemetry settings: either a table of selectable telemetry topics, or a
/// single fixed topic, plus the decimal precision and publish cadence used when
/// streaming the value over the WebSocket connection. Grouped together here (rather
/// than left flat on SketchConfig) since these values are only used by Publisher
/// sketches.
/// </summary>
///
struct TelemetryConfig
{
   /// <summary>Table of selectable telemetry topics, prompted for independently of influx.prompts. Leave empty for a sketch with a single fixed topic (see topic) instead of a user-selectable table.</summary>
   std::span<const char* const> prompts;

   /// <summary>Fixed telemetry topic used when prompts is empty (no selection prompt). Ignored if prompts is non-empty.</summary>
   const char* topic = nullptr;

   /// <summary>Decimal places used when publishing the telemetry value over the WebSocket connection.</summary>
   uint8_t decimals = 2;

   /// <summary>How often (in milliseconds) the telemetry value source is read and published. 0 means every loop() iteration.</summary>
   uint16_t publishIntervalMs = 0;
};
