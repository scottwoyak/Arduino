#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <time.h>
#include <utility>

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>

#include "Status.h"
#include "Timer.h"
#include "Util.h"

// Display-based methods are only available on boards with a display.
// ARDUINO_DISPLAY_SUPPORTED is defined by ArduinoBoard.h when the target board has one.
#ifdef ARDUINO_DISPLAY_SUPPORTED
#include "Format.h"
class ArduinoWithDisplay;
#endif

///
/// <summary>
/// Receives notification of OTA update lifecycle events (update detected, failed, or
/// succeeded). Provides default (no-op) behavior for each event; sketches that need
/// custom behavior should derive from this class and override only the methods they
/// need. Register the instance via OTAUpdater::setHandler() (or the onUpdateAvailable
/// parameter of ArduinoBase/ArduinoWithDisplay::enableOTA()).
/// </summary>
///
class OTAUpdateEventHandler
{
public:
   virtual ~OTAUpdateEventHandler() = default;

   ///
   /// <summary>
   /// Invoked when a pushed update is requested, just before the update is downloaded
   /// and installed.
   /// </summary>
   /// <param name="newVersion">The newly detected version string.</param>
   ///
   virtual void onUpdateAvailable(const char* newVersion)
   {}

   ///
   /// <summary>
   /// Invoked when a detected update fails to download/install.
   /// </summary>
   /// <param name="newVersion">The version that failed to install.</param>
   /// <param name="reason">The error reported by the underlying HTTP update client.</param>
   ///
   virtual void onUpdateFailed(const char* newVersion, const char* reason)
   {}

   ///
   /// <summary>
   /// Invoked when a detected update downloads and installs successfully, just before
   /// the device restarts to apply it.
   /// </summary>
   /// <param name="newVersion">The version that was successfully installed.</param>
   ///
   virtual void onUpdateSucceeded(const char* newVersion)
   {}

   ///
   /// <summary>
   /// Invoked for diagnostic OTA messages that are otherwise only printed to Serial
   /// (e.g. version check failures, missing OTA partition), so a handler can mirror
   /// them elsewhere (e.g. to a LogServer).
   /// </summary>
   /// <param name="message">The diagnostic message.</param>
   ///
   virtual void onLogMessage(const char* message)
   {}
};

///
/// <summary>
/// Adds push-based over-the-air (OTA) firmware update support to a sketch: when an
/// update is requested (e.g. by a Device Server "Update" command carrying a firmware URL),
/// downloads and installs the firmware binary from that URL. Nothing is ever fetched
/// automatically. On display-capable boards, shows the same progress screen used by
/// OTA_Display.ino while the update downloads.
/// </summary>
/// <remarks>
/// Usage:
/// <code>
/// OTAUpdater ota(VERSION, &arduino);
///
/// void loop()
/// {
///    ota.loop();
/// }
/// </code>
/// Call requestUpdate() (e.g. when the Device Server pushes an "Update" command) to start
/// an update; the download itself runs from loop().
/// </remarks>
///
class OTAUpdater
{
private:
   static constexpr uint8_t _HEADER_SIZE = 3;
   static constexpr uint8_t _TEXT_SIZE = 2;
   static constexpr int16_t _PROGRESS_BAR_HEIGHT = 12;
   static constexpr int16_t _PROGRESS_BAR_MARGIN = 4;
   static constexpr uint32_t _RESULT_DELAY_MS = 3000;

   // Minimum time between progress redraws during the download. Throttling by elapsed
   // time (rather than by percent change) keeps the number of draws roughly constant
   // regardless of firmware size or download speed, capping how often the panel is
   // touched - which was found to be the real trigger for display corruption once draws
   // got too frequent, even though each individual draw is already fully serialized
   // against flash writes (see waitDisplay() in _onUpdateProgress()).
   static constexpr uint32_t _PROGRESS_REDRAW_INTERVAL_MS = 300;

   // Timeout for the firmware download's TCP connect and TLS handshake, so a stalled
   // connection fails with a clear error instead of hanging indefinitely.
   static constexpr uint32_t _CONNECT_TIMEOUT_MS = 15000;

   // Maximum time to wait for more data to arrive between chunks while downloading, once
   // the connection is established. The task watchdog is deinitialized for the whole
   // download (see esp_task_wdt_deinit() in _performUpdate()), so a stall here (e.g. the
   // server stops sending data but keeps the TCP connection open) would otherwise hang
   // forever with nothing to catch it; this fails with a clear error instead.
   static constexpr uint32_t _STALL_TIMEOUT_MS = 15000;

   /// <summary>The OTAUpdater most recently constructed, targeted by requestActiveUpdate().</summary>
   static inline OTAUpdater* _active = nullptr;

   const char* _version;
   std::string _firmwareUrl;

   /// <summary>Name of the firmware being installed, parsed from the push URL (e.g. "Gate_Opener.ADAFRUIT_FEATHER_ESP32S3_TFT").</summary>
   std::string _firmwareName;

   /// <summary>True once
   bool _updateRequested = false;

   /// <summary>Version being installed, as reported by the push request.</summary>
   std::string _availableVersion;

   /// <summary>Optional handler notified (with the newly detected version) once a newer version is found, just before the update is installed.</summary>
   OTAUpdateEventHandler* _handler = nullptr;

   /// <summary>Optional status indicator set to FAILED if no OTA download partition is found.</summary>
   IStatus* _status = nullptr;

#ifdef ARDUINO_DISPLAY_SUPPORTED
   ArduinoWithDisplay* _arduino;
   Format _percentFormat{ "###%", Format::Alignment::RIGHT };
   int16_t _downloadRowY = 0;
   int16_t _progressBarY = 0;

   /// <summary>
   /// Last percentage drawn by _onUpdateProgress(), so repeated callbacks for the same
   /// percentage (which fire far more often than the display needs to redraw) skip the
   /// SPI writes entirely. Set to -1 so the first callback always draws.
   /// </summary>
   int16_t _lastDrawnPercent = -1;

   /// <summary>
   /// millis() timestamp of the last progress redraw, so bursts of chunks arriving faster
   /// than the display/panel can settle between draws are throttled by elapsed time
   /// (see _PROGRESS_REDRAW_INTERVAL_MS) rather than only by percentage change.
   /// </summary>
   uint32_t _lastDrawTimeMs = 0;
#endif

   ///
   /// <summary>
   /// Extracts the firmware name from a download URL of the form
   /// ".../api/firmware/{name}/{version}/download". For any other URL, falls back to the
   /// last path segment (without any query string).
   /// </summary>
   /// <param name="url">The firmware download URL.</param>
   /// <returns>The firmware name.</returns>
   ///
   static std::string _parseFirmwareName(const std::string& url)
   {
      constexpr const char* MARKER = "/api/firmware/";
      size_t start = url.find(MARKER);
      if (start != std::string::npos)
      {
         start += strlen(MARKER);
         size_t end = url.find('/', start);
         return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
      }

      std::string path = url.substr(0, url.find('?'));
      return path.substr(path.rfind('/') + 1);
   }

   ///
   /// <summary>
   /// Extracts the firmware version from a download URL of the form
   /// ".../api/firmware/{name}/{version}/download".
   /// </summary>
   /// <param name="url">The firmware download URL.</param>
   /// <returns>The version, or an empty string if the URL isn't of that form.</returns>
   ///
   static std::string _parseFirmwareVersion(const std::string& url)
   {
      constexpr const char* MARKER = "/api/firmware/";
      size_t nameStart = url.find(MARKER);
      if (nameStart == std::string::npos)
      {
         return "";
      }

      nameStart += strlen(MARKER);
      size_t versionStart = url.find('/', nameStart);
      if (versionStart == std::string::npos)
      {
         return "";
      }

      versionStart++;
      size_t versionEnd = url.find('/', versionStart);
      if (versionEnd == std::string::npos)
      {
         return "";
      }

      return url.substr(versionStart, versionEnd - versionStart);
   }

   ///
   /// <summary>
   /// Checks whether the running firmware has a valid OTA download partition
   /// into (i.e. the partition table defines more than one OTA app slot). Without one,
   /// httpUpdate.update() would fail anyway, so this is checked up front to fail fast
   /// with a clear message instead of a confusing download-time error.
   /// </summary>
   /// <returns>True if an OTA download partition is available.</returns>
   ///
   bool _hasDownloadPartition()
   {
      return esp_ota_get_next_update_partition(nullptr) != nullptr;
   }

   ///
   /// <summary>
   /// Reports a diagnostic OTA message: if a handler is registered, mirrors it via
   /// OTAUpdateEventHandler::onLogMessage() (e.g. so SketchBase/ViewerSketch can send it
   /// to the LogServer, which also echoes to Serial via Logger.log()). Otherwise, prints
   /// directly to Serial so the message isn't lost.
   /// </summary>
   /// <param name="message">The message to print and mirror.</param>
   ///
   void _log(const char* message)
   {
      if (_handler != nullptr)
      {
         _handler->onLogMessage(message);
      }
      else
      {
         Serial.println(message);
      }
   }

   ///
   /// <summary>
   /// Reports that no OTA download partition was found: prints to Serial and, on
   /// display-capable boards, the display, sets the status indicator (if any) to
   /// FAILED, then halts the device.
   /// </summary>
   /// <remarks>This function does not return.</remarks>
   ///
   void _reportMissingPartitionAndHalt();

   ///
   /// <summary>
   /// Called after each chunk is written to flash while the firmware download is in
   /// progress. Yields the CPU so other tasks get a chance to run between chunks and, on
   /// display-capable boards, also reports progress as a percentage and a fill bar
   /// (throttled to redraw only when the percentage changes, to minimize SPI writes).
   /// </summary>
   /// <param name="current">Number of bytes downloaded so far.</param>
   /// <param name="total">Total number of bytes to download.</param>
   ///
   void _onUpdateProgress(int current, int total);

   ///
   /// <summary>
   /// Downloads and installs the firmware binary via a manual chunked HTTP read / flash
   /// write loop, showing progress and the final result on the display (if present).
   /// Restarts the device on success. Progress is drawn only between chunk writes, so a
   /// display SPI transaction is never in flight while flash writes (which briefly disable
   /// both cores' flash caches) are happening. Also temporarily disables both cores'
   /// idle-task watchdogs for the duration of the (blocking) download, since flash writes
   /// during the update briefly halt the other core and could otherwise starve its idle
   /// task long enough to trip the task watchdog and panic-reset the device mid-download.
   /// </summary>
   ///
   void _performUpdate();

#ifdef ARDUINO_DISPLAY_SUPPORTED
   ///
   /// <summary>
   /// Clears the display immediately, if one is present. Used to clear stale content as
   /// soon as an update is detected, rather than leaving it showing through the delay
   /// before _performUpdate() gets around to its own clear.
   /// </summary>
   ///
   void _clearDisplayIfPresent();
#endif

public:
public:
   ///
   /// <summary>
   /// Constructs an OTAUpdater, without a display (serial-only boards). Firmware is never
   /// fetched automatically; an update is only installed after requestUpdate() is called
   /// (e.g. when the Device Server pushes an "Update" command).
   /// </summary>
   /// <param name="version">This sketch's own version string (e.g. "v1.0").</param>
   ///
   OTAUpdater(const char* version)
	  : _version(version)
   {
	  _active = this;
   }

#ifdef ARDUINO_DISPLAY_SUPPORTED
   ///
   /// <summary>
   /// Constructs an OTAUpdater that shows download progress on the given display.
   /// </summary>
   /// <param name="version">This sketch's own version string (e.g. "v1.0").</param>
   /// <param name="arduino">Display to show progress and results on while updating.</param>
   ///
   OTAUpdater(const char* version, ArduinoWithDisplay* arduino)
	  : _version(version), _arduino(arduino)
   {
	  _active = this;
   }
#endif

   ///
   /// <summary>
   /// Registers a handler notified when a pushed update starts, fails, or succeeds.
   /// </summary>
   /// <param name="handler">Handler to notify.</param>
   ///
   void setHandler(OTAUpdateEventHandler* handler)
   {
	  _handler = handler;
   }

   ///
   /// <summary>
   /// Registers a status indicator to set to FAILED if no OTA download partition is found.
   /// </summary>
   /// <param name="status">Status indicator to update.</param>
   ///
   void setStatus(IStatus* status)
   {
	  _status = status;
   }

   ///
   /// <summary>
   /// Requests an update on the active OTAUpdater (the most recently constructed one).
   /// Does nothing if no OTAUpdater exists. Safe to call from a message callback; the
   /// download itself is deferred to loop().
   /// </summary>
   /// <param name="url">URL of the firmware binary to download.</param>
   /// <param name="version">Version being pushed, used for logging (may be nullptr).</param>
   ///
   static void requestActiveUpdate(const char* url, const char* version = nullptr)
   {
	  if (_active != nullptr)
	  {
		 _active->requestUpdate(url, version);
	  }
   }

   ///
   /// <summary>
   /// Requests that the firmware at the given URL be downloaded and installed. The
   /// download is deferred until the next loop() call.
   /// </summary>
   /// <param name="url">URL of the firmware binary to download.</param>
   /// <param name="version">Version being pushed, used for logging (may be nullptr).</param>
   ///
   void requestUpdate(const char* url, const char* version = nullptr)
   {
	  _firmwareUrl = url;
	  _firmwareName = _parseFirmwareName(_firmwareUrl);
	  _availableVersion = (version != nullptr && *version != '\0') ? version : _parseFirmwareVersion(_firmwareUrl);
	  if (_availableVersion.empty())
	  {
		 _availableVersion = "pushed firmware";
	  }
	  _updateRequested = true;
   }

   ///
   /// <summary>
   /// Call every loop() iteration. Downloads and installs the firmware if an update was
   /// requested via requestUpdate(), restarting the device on success. Halts the device
   /// if the running firmware has no OTA download partition to update into.
   /// </summary>
   ///
   void loop()
   {
	  if (!_updateRequested)
	  {
		 return;
	  }
	  _updateRequested = false;

	  if (!_hasDownloadPartition())
	  {
		 _reportMissingPartitionAndHalt();
	  }

	  if (_handler != nullptr)
	  {
		 _handler->onUpdateAvailable(_availableVersion.c_str());
	  }

	  _log((std::string("Updating firmware to ") + _availableVersion).c_str());
	  _log("Downloading firmware");

#ifdef ARDUINO_DISPLAY_SUPPORTED
	  _clearDisplayIfPresent();
#endif

	  if (_status != nullptr)
	  {
		 _status->setStatus(Status::UPDATING);
	  }

	  _performUpdate();

	  // Only reached if the update failed (ESP.restart() is called directly on success).
	  if (_status != nullptr)
	  {
		 _status->setStatus(Status::READY);
	  }
   }
};

// On non-display boards, ArduinoWithDisplay is never referenced, so the out-of-line
// method bodies can be included right here. On display-capable boards, ArduinoWithDisplay.h
// includes OTAUpdaterImpl.h itself, once its own class definition is complete, since
// those method bodies need ArduinoWithDisplay to be a complete type.
#ifndef ARDUINO_DISPLAY_SUPPORTED
#include "OTAUpdaterImpl.h"
#endif
