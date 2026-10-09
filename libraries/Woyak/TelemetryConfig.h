#pragma once

#include <span>
#include <stdint.h>

#include "TelemetryEndpoint.h"
#include "WiFiSettings.h"

///
/// <summary>
/// Telemetry settings for a sketch: the topic (a table of selectable topics, or a single
/// fixed topic), the decimal precision and publish cadence used when streaming values,
/// and the server to connect to (primary and optional fallback endpoint, WebSocket path
/// and per-role authentication tokens). See TELEMETRY_RASPBERRY_ENDPOINT and similar in
/// WiFiSettings.h for the standard server values.
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

   /// <summary>Maximum rate (messages per second) a publisher sends values. Values set faster are held and only the latest is sent when the next slot opens. 0 means no cap.</summary>
   uint16_t maxPublishRatePerSec = 30;

   /// <summary>Server endpoint tried first.</summary>
   TelemetryEndpoint primary = TELEMETRY_RASPBERRY_ENDPOINT;

   /// <summary>Server endpoint alternated with if the primary is not ready. Leave the port 0 for no fallback.</summary>
   TelemetryEndpoint fallback = TELEMETRY_PRODUCTION_ENDPOINT;

   /// <summary>Token sent by clients that publish.</summary>
   const char* deviceToken = TELEMETRY_DEVICE_TOKEN;

   /// <summary>Token sent by clients that subscribe.</summary>
   const char* clientToken = TELEMETRY_CLIENT_TOKEN;

   /// <summary>WebSocket path.</summary>
   const char* path = TELEMETRY_PATH;
};
