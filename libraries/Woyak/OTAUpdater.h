#pragma once

#include <functional>
#include <string>
#include <utility>

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>

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
   {
   }

   ///
   /// <summary>
   /// Invoked when a detected update fails to download/install.
   /// </summary>
   /// <param name="newVersion">The version that failed to install.</param>
   /// <param name="reason">The error reported by the underlying HTTP update client.</param>
   ///
   virtual void onUpdateFailed(const char* newVersion, const char* reason)
   {
   }

   ///
   /// <summary>
   /// Invoked when a detected update downloads and installs successfully, just before
   /// the device restarts to apply it.
   /// </summary>
   /// <param name="newVersion">The version that was successfully installed.</param>
   ///
   virtual void onUpdateSucceeded(const char* newVersion)
   {
   }
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

   static constexpr uint8_t _HEADER_SIZE = 3;
   static constexpr uint8_t _TEXT_SIZE = 2;
   static constexpr int16_t _PROGRESS_BAR_HEIGHT = 12;
   static constexpr int16_t _PROGRESS_BAR_MARGIN = 4;
   static constexpr uint32_t _RESULT_DELAY_MS = 3000;

   const char* _version;
   std::string _versionUrl;
   std::string _firmwareUrl;
   TimerSecs _checkTimer;

   /// <summary>Version detected by the last _isUpdateAvailable() call that returned true.</summary>
   std::string _availableVersion;

   /// <summary>Optional handler notified (with the newly detected version) once a newer version is found, just before the update is installed.</summary>
   OTAUpdateEventHandler* _handler = nullptr;

#ifdef ARDUINO_DISPLAY_SUPPORTED
   ArduinoWithDisplay* _arduino;
   Format _percentFormat{ "###%", Format::Alignment::RIGHT };
   int16_t _downloadRowY = 0;
   int16_t _progressBarY = 0;
#endif

   // The active instance being updated, used by the static HTTPUpdate progress callback
   // since HTTPUpdate only supports a plain function pointer (no captured context).
   static inline OTAUpdater* _active = nullptr;

   ///
   /// <summary>
   /// Derives this sketch's firmware/version-check URLs from its name, per convention: each
   /// sketch publishes to a release tagged with its own name (e.g. "Wind_Publisher"), with
   /// "{sketchName}.ino.bin" and "version.txt" as assets alongside each other.
   /// </summary>
   /// <param name="sketchName">This sketch's name (e.g. "Wind_Publisher"), also used as the release tag.</param>
   /// <returns>The derived { firmwareUrl, versionUrl } pair.</returns>
   ///
   static std::pair<std::string, std::string> _deriveUrls(const char* sketchName)
   {
      std::string releaseUrl = std::string(_RELEASES_BASE_URL) + "/" + sketchName + "/";
      return { releaseUrl + sketchName + ".ino.bin", releaseUrl + "version.txt" };
   }

   ///
   /// <summary>
   /// Fetches the version text file and returns whether it differs from this sketch's
   /// own version (a fresh fetch failure is treated as "no update available").
   /// </summary>
   /// <returns>True if a different version is available on the server.</returns>
   ///
   bool _isUpdateAvailable()
   {
      HTTPClient http;
      http.begin(_versionUrl.c_str());
      http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
      int httpCode = http.GET();

      if (httpCode != HTTP_CODE_OK)
      {
         Serial.printf("OTAUpdater: version check HTTP GET failed, code: %d\n", httpCode);
         http.end();
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

      bool updateAvailable = !serverVersion.equals(_version);
      if (updateAvailable)
      {
         _availableVersion = serverVersion.c_str();
         Serial.printf("OTAUpdater: Downloading %s\n", serverVersion.c_str());
      }

      return updateAvailable;
   }

#ifdef ARDUINO_DISPLAY_SUPPORTED
   ///
   /// <summary>
   /// Reports OTA download progress as a percentage (and a fill bar) on the display.
   /// </summary>
   /// <param name="current">Number of bytes downloaded so far.</param>
   /// <param name="total">Total number of bytes to download.</param>
   ///
   void _onUpdateProgress(int current, int total);

   static void _onUpdateProgressHandler(int current, int total) { _active->_onUpdateProgress(current, total); }
#endif

   ///
   /// <summary>
   /// Downloads and installs the firmware binary, showing progress and the final result
   /// on the display (if present). Restarts the device on success.
   /// </summary>
   ///
   void _performUpdate();

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
      : _version(version), _checkTimer(checkIntervalSecs)
   {
      std::tie(_firmwareUrl, _versionUrl) = _deriveUrls(sketchName);
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
      : _version(version), _checkTimer(checkIntervalSecs), _arduino(arduino)
   {
      std::tie(_firmwareUrl, _versionUrl) = _deriveUrls(sketchName);
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
   /// Checks (over the version URL) whether a newer firmware version is available and, if
   /// so, downloads and installs it, restarting the device on success. WiFi must already
   /// be connected before calling this.
   /// </summary>
   ///
   void checkNow()
   {
      if (_isUpdateAvailable())
      {
         if (_handler != nullptr)
         {
            _handler->onUpdateAvailable(_availableVersion.c_str());
         }
         _performUpdate();
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
