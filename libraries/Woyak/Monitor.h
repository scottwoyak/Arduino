#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID,
// WIFI_PASSWORD, INFLUXDB_URL, and INFLUXDB_ORG are defined). This mirrors the include
// order already used by Publisher-based sketches (see Publisher.h).

#include "SketchBase.h"

///
/// <summary>
/// Owns the initialization and loop sequence shared by every InfluxDB-only monitor
/// sketch: banner, force-prompt window, sensor init, site resolution, WiFi, rebooter,
/// OTA, InfluxDB setup (including a single startup log point with the sketch name,
/// version, and Influx path), and the standard enclosure/CPU points. Unlike Publisher,
/// Monitor doesn't stream any value over a telemetry WebSocket connection. A sketch
/// registers its sensors, extra Influx points, and per-loop work via the methods below
/// before calling begin(), then calls begin() once from setup() and loop() once from
/// loop(). Shared lifecycle logic lives in SketchBase; this class only supplies the
/// Monitor-specific hook overrides.
/// </summary>
///
class Monitor : public SketchBase
{
protected:
   ///
   /// <summary>Monitor has no selectable site table fallback beyond its fixed site.</summary>
   ///
   SiteConfig _resolveFixedSite() override
   {
      return _config.fixedSite;
   }

   ///
   /// <summary>Monitor always uses Influx, regardless of whether a site table was configured.</summary>
   ///
   bool _shouldUseInflux(bool /*hasSiteTable*/) override
   {
      return true;
   }

   ///
   /// <summary>Builds the startup log message: sketch name and Influx bucket/site path.</summary>
   ///
   std::string _buildStartupMessage(const std::string& influxPath) override
   {
      return std::string("Starting ") + _config.sketchName + " (" + influxPath + ")";
   }

public:
   ///
   /// <summary>
   /// Creates a Monitor bound to the given board and configuration. Register sensors,
   /// extra Influx points, and loop hooks afterward, then call begin().
   /// </summary>
   /// <param name="arduino">The board wrapper (used as the status indicator directly if it implements IStatus itself; otherwise its onboard NeoPixel LED is used).</param>
   /// <param name="config">Shared monitor configuration.</param>
   ///
   Monitor(Arduino* arduino, const SketchConfig& config)
      : SketchBase(arduino, config)
   {
   }
};
