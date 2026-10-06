#pragma once

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
