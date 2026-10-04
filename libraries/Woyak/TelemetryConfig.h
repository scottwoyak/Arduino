#pragma once

#include <span>
#include <stdint.h>

///
/// <summary>
/// One server endpoint (host, port, TLS) a telemetry client can connect to.
/// </summary>
///
struct TelemetryEndpoint
{
   const char* host = nullptr;
   uint16_t port = 0;
   bool useTls = false;
};

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

   /// <summary>Server endpoint tried first.</summary>
   TelemetryEndpoint primary;

   /// <summary>Server endpoint alternated with if the primary is not ready. Leave the port 0 for no fallback.</summary>
   TelemetryEndpoint fallback;

   /// <summary>Token sent by clients that publish.</summary>
   const char* deviceToken = nullptr;

   /// <summary>Token sent by clients that subscribe.</summary>
   const char* clientToken = nullptr;

   /// <summary>WebSocket path.</summary>
   const char* path = "/ws";
};
