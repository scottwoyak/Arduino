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
   /// Invoked when checkNow()/loop() finds a newer version available, just before the
   /// update is downloaded and installed.
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
/// Adds over-the-air (OTA) firmware update support to a sketch: periodically (or on
/// demand) fetches a small version text file, compares it to the sketch's own version,
/// and if a newer version is available downloads and installs the firmware binary. On
/// display-capable boards, shows the same progress screen used by OTA_Display.ino while
/// the update downloads.
/// </summary>
/// <remarks>
/// Usage:
/// <code>
/// OTAUpdater ota(VERSION, VERSION_URL, FIRMWARE_URL, &arduino);
///
/// void loop()
/// {
///    ota.loop();
/// }
/// </code>
/// Call checkNow() instead of (or in addition to) loop() to trigger a check immediately,
/// e.g. from a button press.
/// </remarks>
///
class OTAUpdater
{
public:
   // Default interval between periodic OTA checks, used when a sketch doesn't specify one.
   static constexpr float DEFAULT_CHECK_INTERVAL_SECS = 10.0f * 60.0f;

private:
   // Base URL for this project's GitHub releases; each sketch publishes its firmware/version
   // files as assets under a release tagged with its own sketch name (see _deriveUrls()).
   static constexpr auto _RELEASES_BASE_URL = "https://github.com/scottwoyak/Arduino/releases/download";

   // ARDUINO_BOARD_VARIANT_ID (defined per-branch in ArduinoBoard.h) identifies this
   // sketch's physical wiring variant, so firmware asset names can be board-qualified
   // (see _deriveUrls()), allowing a single release to host binaries for multiple boards.
   // This is distinct from the raw ARDUINO_BOARD macro, since multiple wiring variants
   // (e.g. Hosyond Viewer vs. generic Playground) can share the same underlying Arduino
   // IDE board type.
   static constexpr auto _BOARD_ID = ARDUINO_BOARD_VARIANT_ID;

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

   const char* _version;
   std::string _versionUrl;
   std::string _firmwareUrl;
   TimerSecs _checkTimer;

   // The originally configured check interval, in seconds. _checkTimer's own duration is
   // repeatedly overwritten by _secsUntilNextAlignedCheck() (to land the *next* check on
   // an aligned wall-clock boundary), so this is kept separately as the stable interval
   // to align against; using _checkTimer's live duration for that calculation would
   // otherwise compound each time it's re-armed, collapsing the interval towards zero.
   unsigned long _checkIntervalSecs;

   /// <summary>Version detected by the last _isUpdateAvailable() call that returned true.</summary>
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
   /// Derives this sketch's firmware/version-check URLs from its name, per convention: each
   /// sketch publishes to a release tagged with its own name (e.g. "Wind_Publisher"), with
   /// "{sketchName}.{boardId}.ino.bin" and "{sketchName}.{boardId}.version.txt" as assets
   /// alongside each other. Both the firmware and version-check asset names are
   /// board-qualified (via ARDUINO_BOARD_VARIANT_ID, see _BOARD_ID) so a single release can host
   /// independently-versioned binaries for multiple boards without one board's publish
   /// causing another board to redownload an unchanged binary.
   /// </summary>
   /// <param name="sketchName">This sketch's name (e.g. "Wind_Publisher"), also used as the release tag.</param>
   /// <returns>The derived { firmwareUrl, versionUrl } pair.</returns>
   ///
   static std::pair<std::string, std::string> _deriveUrls(const char* sketchName)
   {
      std::string releaseUrl = std::string(_RELEASES_BASE_URL) + "/" + sketchName + "/";
      std::string boardQualifiedName = std::string(sketchName) + "." + _BOARD_ID;
      return { releaseUrl + boardQualifiedName + ".ino.bin", releaseUrl + boardQualifiedName + ".version.txt" };
   }

   ///
   /// <summary>
   /// Computes the number of seconds from now until the next wall-clock boundary that is
   /// an exact multiple of the configured check interval (e.g. every 10 minutes results in
   /// checks landing on 6:00, 6:10, 6:20, etc). Falls back to the full interval if the
   /// system clock isn't synced yet (time(nullptr) is unreasonably small).
   /// </summary>
   /// <returns>Seconds until the next aligned check.</returns>
   ///
   unsigned long _secsUntilNextAlignedCheck() const
   {
      unsigned long intervalSecs = _checkIntervalSecs;
      if (intervalSecs == 0)
      {
         return 0;
      }

      time_t now = time(nullptr);
      if (now < 1000000000l)
      {
         // Clock not synced yet; fall back to a plain interval.
         return intervalSecs;
      }

      unsigned long secsIntoInterval = static_cast<unsigned long>(now) % intervalSecs;
      return (secsIntoInterval == 0) ? intervalSecs : (intervalSecs - secsIntoInterval);
   }

   ///
   /// <summary>
   /// Checks whether the running firmware has a valid OTA download partition to update
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
   /// Fetches the version text file and returns whether it differs from this sketch's
   /// own version (a fresh fetch failure is treated as "no update available"), unless
   /// force is true, in which case an update is always reported as available (using the
   /// current version as a fallback if the version check itself fails).
   /// </summary>
   /// <param name="force">If true, report an update as available regardless of the server's version.</param>
   /// <returns>True if an update should be downloaded and installed.</returns>
   ///
   bool _isUpdateAvailable(bool force = false)
   {
      HTTPClient http;
      http.begin(_versionUrl.c_str());
      http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
      int httpCode = http.GET();

      if (httpCode != HTTP_CODE_OK)
      {
         _log((std::string("OTAUpdater: version check HTTP GET failed, code: ") + std::to_string(httpCode) + " (" + HTTPClient::errorToString(httpCode).c_str() + "), url: " + _versionUrl).c_str());
         http.end();

         if (force)
         {
            _availableVersion = _version;
            _log("OTAUpdater: forcing update using current version (version check failed)");
            return true;
         }

         return false;
      }

      String serverVersion = http.getString();
      serverVersion.trim();
      http.end();

      // version.txt is a quoted string literal (so it can be #included directly as the
      // local VERSION), so strip matching surrounding quotes before comparing.
      if (serverVersion.length() >= 2 && serverVersion.startsWith("\"") && serverVersion.endsWith("\""))
      {
         serverVersion = serverVersion.substring(1, serverVersion.length() - 1);
      }

         bool updateAvailable = force || _isNewerVersion(serverVersion.c_str(), _version);
         if (updateAvailable)
         {
            _availableVersion = serverVersion.c_str();
         }

         return updateAvailable;
      }

      ///
      /// <summary>
      /// Parses a "major.minor.build" version string into its numeric components,
      /// tolerating an optional leading 'v'/'V' and missing trailing components (treated as
      /// 0), e.g. "v2.1" is parsed as 2.1.0.
      /// </summary>
      /// <param name="version">Version string to parse.</param>
      /// <returns>The parsed { major, minor, build } components.</returns>
      ///
      std::array<uint32_t, 3> _parseVersion(const char* version)
      {
         std::array<uint32_t, 3> parts = { 0, 0, 0 };

         if (version == nullptr)
         {
            return parts;
         }

         if (*version == 'v' || *version == 'V')
         {
            version++;
         }

         sscanf(version, "%lu.%lu.%lu", &parts[0], &parts[1], &parts[2]);
         return parts;
      }

      ///
      /// <summary>
      /// Compares two version strings by their major, minor, and build components (see
      /// _parseVersion()) to determine whether candidateVersion is strictly newer than
      /// currentVersion.
      /// </summary>
      /// <param name="candidateVersion">The version fetched from the server.</param>
      /// <param name="currentVersion">This sketch's own version.</param>
      /// <returns>True if candidateVersion is greater than currentVersion.</returns>
      ///
      bool _isNewerVersion(const char* candidateVersion, const char* currentVersion)
      {
         std::array<uint32_t, 3> candidate = _parseVersion(candidateVersion);
         std::array<uint32_t, 3> current = _parseVersion(currentVersion);

         return candidate > current;
      }

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
   ///
   /// <summary>
   /// Constructs an OTAUpdater, without a display (serial-only boards). The firmware and
   /// version-check URLs are both derived from sketchName per convention: this sketch
   /// publishes to a GitHub release tagged with its own name, alongside a "version.txt".
   /// </summary>
   /// <param name="version">This sketch's own version string (e.g. "v1.0").</param>
   /// <param name="sketchName">This sketch's name (e.g. "Wind_Publisher"), used to derive its release URLs.</param>
   /// <param name="checkIntervalSecs">How often (in seconds) loop() checks for an update; defaults to 10 minutes.</param>
   ///
   OTAUpdater(const char* version, const char* sketchName, float checkIntervalSecs = DEFAULT_CHECK_INTERVAL_SECS)
      : _version(version), _checkTimer(checkIntervalSecs), _checkIntervalSecs(static_cast<unsigned long>(checkIntervalSecs))
   {
      std::tie(_firmwareUrl, _versionUrl) = _deriveUrls(sketchName);
      _checkTimer.setDurationMs(_secsUntilNextAlignedCheck() * 1000UL);
   }

#ifdef ARDUINO_DISPLAY_SUPPORTED
   ///
   /// <summary>
   /// Constructs an OTAUpdater that shows download progress on the given display. The
   /// firmware and version-check URLs are both derived from sketchName per convention:
   /// this sketch publishes to a GitHub release tagged with its own name, alongside a
   /// "version.txt".
   /// </summary>
   /// <param name="version">This sketch's own version string (e.g. "v1.0").</param>
   /// <param name="sketchName">This sketch's name (e.g. "Wind_Publisher"), used to derive its release URLs.</param>
   /// <param name="arduino">Display to show progress and results on while updating.</param>
   /// <param name="checkIntervalSecs">How often (in seconds) loop() checks for an update; defaults to 10 minutes.</param>
   ///
   OTAUpdater(const char* version, const char* sketchName, ArduinoWithDisplay* arduino, float checkIntervalSecs = DEFAULT_CHECK_INTERVAL_SECS)
      : _version(version), _checkTimer(checkIntervalSecs), _checkIntervalSecs(static_cast<unsigned long>(checkIntervalSecs)), _arduino(arduino)
   {
      std::tie(_firmwareUrl, _versionUrl) = _deriveUrls(sketchName);
      _checkTimer.setDurationMs(_secsUntilNextAlignedCheck() * 1000UL);
   }
#endif

   ///
   /// <summary>
   /// Registers a handler notified whenever checkNow()/loop() finds a newer version
   /// available, just before the update is downloaded and installed.
   /// </summary>
   /// <param name="handler">Handler to notify with the newly detected version string.</param>
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
   /// Checks (over the version URL) whether a newer firmware version is available and, if
   /// so, downloads and installs it, restarting the device on success. WiFi must already
   /// be connected before calling this. Halts the device (see _reportMissingPartitionAndHalt())
   /// if the running firmware has no OTA download partition to update into.
   /// </summary>
   /// <param name="force">If true, downloads and installs the current OTA firmware regardless of version.</param>
   ///
   void checkNow(bool force = false)
   {
      if (!_hasDownloadPartition())
      {
         _reportMissingPartitionAndHalt();
      }

      if (_isUpdateAvailable(force))
      {
         if (_handler != nullptr)
         {
            _handler->onUpdateAvailable(_availableVersion.c_str());
         }

         _log((std::string("OTAUpdater: Downloading ") + _availableVersion).c_str());

#ifdef ARDUINO_DISPLAY_SUPPORTED
         // Clear immediately on detecting an update, rather than leaving whatever was on
         // screen showing through the version check / HTTP connect delay that happens
         // before _performUpdate() gets around to its own printInitHeader()/clearDisplay().
         _clearDisplayIfPresent();
#endif

         if (_status != nullptr)
         {
            _status->setStatus(Status::UPDATING);
         }

         _performUpdate();

         // Only reached if the update failed or wasn't actually applied (ESP.restart()
         // is called directly on success), so restore the prior status.
         if (_status != nullptr)
         {
            _status->setStatus(Status::READY);
         }
      }
   }

   ///
   /// <summary>
   /// Call every loop() iteration to periodically check for an update, at the interval
   /// configured in the constructor. Does nothing if checkIntervalSecs was 0.
   /// </summary>
   ///
   void loop()
   {
      if (_checkTimer.getDurationMs() != 0 && _checkTimer.ready())
      {
         checkNow();

         // Re-arm the timer so the *next* check lands on the next even wall-clock
         // boundary (e.g. 6:00, 6:10, 6:20...) rather than drifting from whenever
         // this check happened to run.
         _checkTimer.setDurationMs(_secsUntilNextAlignedCheck() * 1000UL);
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
