#pragma once

// Requires the sketch to have already included, in order: ArduinoBoard.h (so the
// board-specific Arduino type is defined), and WiFiSettings.h (so WIFI_SSID and
// WIFI_PASSWORD are defined). This mirrors the include order used by Monitor-/
// Publisher-based sketches (see SketchBase.h).

#include <string>

#include "ArduinoBase.h"
#include "Status.h"

///
/// <summary>
/// Owns the boot/init sequence shared by every display-only "Viewer" sketch (e.g.
/// Gate_Viewer, Wind_Viewer): printing the sketch name/version banner to Serial and the
/// display, connecting to WiFi, and (optionally) enabling OTA firmware updates. Unlike
/// Monitor/Publisher (see SketchBase), a Viewer doesn't post to InfluxDB or track a
/// site/location; it only renders telemetry it receives. A sketch constructs a
/// ViewerSketch, calls begin() once from setup() (after registering its own telemetry
/// client(s) and before any sketch-specific WiFi-dependent setup), and calls
/// checkForOTA() once from loop().
/// </summary>
///
class ViewerSketch
{
protected:
   /// <summary>Board wrapper.</summary>
   Arduino* _arduino;

   /// <summary>Sketch name, printed at boot and used as the OTA update identifier.</summary>
   const char* _sketchName;

   /// <summary>Sketch version string, printed at boot and used for OTA update checks. Leave null to skip OTA entirely.</summary>
   const char* _version;

   /// <summary>Status indicator driven through the WiFi/OTA phases of begin().</summary>
   IStatus* _status;

   /// <summary>If true, enables OTA firmware updates via arduino.enableOTA() at the end of begin().</summary>
   bool _enableOTA;

public:
   ///
   /// <summary>
   /// Creates a ViewerSketch bound to the given board, sketch identity, and status
   /// indicator.
   /// </summary>
   /// <param name="arduino">The board wrapper.</param>
   /// <param name="sketchName">Sketch name, printed at boot and used as the OTA update identifier.</param>
   /// <param name="version">Sketch version string, printed at boot and used for OTA update checks.</param>
   /// <param name="status">Status indicator driven through the WiFi/OTA phases of begin().</param>
   /// <param name="enableOTA">If true, enables OTA firmware updates at the end of begin().</param>
   ///
   ViewerSketch(Arduino* arduino, const char* sketchName, const char* version, IStatus* status, bool enableOTA = false)
      : _arduino(arduino), _sketchName(sketchName), _version(version), _status(status), _enableOTA(enableOTA)
   {
      ASSERT(arduino != nullptr);
   }

   ///
   /// <summary>
   /// Prints the sketch name/version banner to Serial and the display, connects to
   /// WiFi, and enables OTA if configured. Call once from setup(), after registering any
   /// telemetry client handlers so they're ready to start connecting once WiFi is up.
   /// </summary>
   ///
   void begin()
   {
      std::string sketchLabel = std::string(_sketchName) + ", " + _version;
      _arduino->println("Sketch...", sketchLabel.c_str());

      _arduino->beginInit();

      _arduino->initWifi(WIFI_SSID, WIFI_PASSWORD, _status);

      if (_enableOTA)
      {
         _arduino->enableOTA(_version, _sketchName);
      }
   }

   ///
   /// <summary>
   /// Checks for a pending OTA firmware update. Call once from loop(), before any other
   /// per-loop work. Does nothing if OTA wasn't enabled.
   /// </summary>
   ///
   void checkForOTA()
   {
      if (_enableOTA)
      {
         _arduino->checkForOTA();
      }
   }
};
