#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID and
// WIFI_PASSWORD are defined). This mirrors the include order used by
// ViewerSketch/InfluxSketchBase-derived sketches.

#include "SketchBase.h"

///
/// <summary>
/// Minimal sketch base for a device that only needs to connect to the Device Hub: it
/// prints the sketch name/version startup info, connects to WiFi, enables OTA and the
/// rebooter if configured, registers/initializes any sensors added via addSensor(), and
/// starts the Device Server connection (logging, GetStatus, remote update). Unlike
/// MonitorSketch/PublisherSketch (see InfluxSketchBase) it doesn't post to InfluxDB, and
/// unlike ViewerSketch it doesn't connect to the telemetry server. A sketch constructs a
/// DeviceSketch, calls begin() once from setup(), and calls loop() once from loop().
/// </summary>
///
class DeviceSketch : public SketchBase
{
public:
   ///
   /// <summary>
   /// Creates a DeviceSketch.
   /// </summary>
   /// <param name="config">Shared configuration (sketch name/version, OTA, rebooter, CPU frequency).</param>
   ///
   explicit DeviceSketch(const SketchConfig& config)
      : SketchBase(config)
   {
   }

   ///
   /// <summary>
   /// Initializes the board (arduino.begin()) and prints the sketch name/version startup
   /// info to Serial and the display. Call once from setup(), followed by beginConnect().
   /// </summary>
   ///
   void beginBanner()
   {
      arduino.begin();
      _printStartupInfo();
   }

   ///
   /// <summary>
   /// Connects to WiFi, enables OTA/the rebooter if configured, and initializes any
   /// sensors registered via addSensor(). Call once from setup(), after beginBanner().
   /// Followed by any other WiFi-dependent setup (e.g. a telemetry client) and finally
   /// beginLogger().
   /// </summary>
   ///
   void beginConnect()
   {
      _beginConnect();
      _initSensors();
   }

   ///
   /// <summary>
   /// Starts the Device Server connection (logging, GetStatus, remote update). Call once
   /// from setup(), as the last setup step, so no other log lines interleave with its
   /// pending "Hub... " completion.
   /// </summary>
   ///
   void beginLogger()
   {
      _beginLogger();
   }

   ///
   /// <summary>
   /// Runs the standard boot sequence: beginBanner(), beginConnect() and beginLogger().
   /// Call once from setup(), after registering any sensors with addSensor() and any
   /// callbacks with onStatus(). A sketch with its own WiFi-dependent setup should call
   /// the three steps individually instead.
   /// </summary>
   ///
   void begin()
   {
      beginBanner();
      beginConnect();
      beginLogger();
   }

   ///
   /// <summary>
   /// Runs the standard per-loop step: WiFi check, watchdog, OTA, Device Server polling,
   /// and status indicators. Call once from loop().
   /// </summary>
   ///
   void loop()
   {
      _loopStep();
   }
};
