#pragma once

#include <string>

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>

#include "Timer.h"
#include "Util.h"

// Display-based methods are only available on boards with a display.
// ARDUINO_DISPLAY_SUPPORTED is defined by ArduinoBoard.h when the target board has one.
#ifdef ARDUINO_DISPLAY_SUPPORTED
#include "ArduinoWithDisplay.h"
#endif

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
private:
   static constexpr uint8_t _HEADER_SIZE = 3;
   static constexpr uint8_t _TEXT_SIZE = 2;
   static constexpr int16_t _PROGRESS_BAR_HEIGHT = 12;
   static constexpr int16_t _PROGRESS_BAR_MARGIN = 4;
   static constexpr uint32_t _RESULT_DELAY_MS = 3000;

   const char* _version;
   std::string _versionUrl;
   const char* _firmwareUrl;
   TimerSecs _checkTimer;

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
   /// Derives the version-check URL from a firmware URL, per convention: version.txt is
   /// published alongside the firmware binary in the same directory (e.g.
   /// ".../releases/download/Foo/Foo.ino.bin" becomes ".../releases/download/Foo/version.txt").
   /// </summary>
   /// <param name="firmwareUrl">URL of the firmware .bin.</param>
   /// <returns>The derived version.txt URL.</returns>
   ///
   static std::string _deriveVersionUrl(const char* firmwareUrl)
   {
      std::string url(firmwareUrl);
      size_t lastSlash = url.find_last_of('/');
      return url.substr(0, lastSlash + 1) + "version.txt";
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
         Serial.print("OTAUpdater: version check HTTP GET failed, code: ");
         Serial.println(httpCode);
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

      Serial.print("OTAUpdater: local version ");
      Serial.print(_version);
      Serial.print(", server version ");
      Serial.println(serverVersion);

      return !serverVersion.equals(_version);
   }

#ifdef ARDUINO_DISPLAY_SUPPORTED
   ///
   /// <summary>
   /// Reports OTA download progress as a percentage (and a fill bar) on the display.
   /// </summary>
   /// <param name="current">Number of bytes downloaded so far.</param>
   /// <param name="total">Total number of bytes to download.</param>
   ///
   void _onUpdateProgress(int current, int total)
   {
      uint8_t percent = (total > 0) ? (current * 100 / total) : 0;

      _arduino->setTextSize(_TEXT_SIZE);
      _arduino->setCursorY(_downloadRowY);
      _arduino->printR((float)percent, _percentFormat, Color::VALUE);

      int16_t barWidth = _arduino->width() - 2 * _PROGRESS_BAR_MARGIN;
      int16_t fillWidth = barWidth * percent / 100;
      _arduino->fillRect(_PROGRESS_BAR_MARGIN, _progressBarY, fillWidth, _PROGRESS_BAR_HEIGHT, Color::LIME);
   }

   static void _onUpdateProgressHandler(int current, int total) { _active->_onUpdateProgress(current, total); }
#endif

   ///
   /// <summary>
   /// Downloads and installs the firmware binary, showing progress and the final result
   /// on the display (if present). Restarts the device on success.
   /// </summary>
   ///
   void _performUpdate()
   {
#ifdef ARDUINO_DISPLAY_SUPPORTED
      if (_arduino != nullptr)
      {
         _arduino->printHeader("Updating Firmware");
         _arduino->setTextSize(_TEXT_SIZE);
         _arduino->print("Downloading...", Color::LABEL);
         _downloadRowY = _arduino->getCursorY();

         _progressBarY = _downloadRowY + _arduino->charH() + _PROGRESS_BAR_MARGIN;
         _arduino->fillRect(_PROGRESS_BAR_MARGIN, _progressBarY, _arduino->width() - 2 * _PROGRESS_BAR_MARGIN, _PROGRESS_BAR_HEIGHT, Color::DARKGRAY);
      }
#endif

      WiFiClientSecure client;
      client.setInsecure();

      _active = this;

#ifdef ARDUINO_DISPLAY_SUPPORTED
      if (_arduino != nullptr)
      {
         httpUpdate.onProgress(_onUpdateProgressHandler);
      }
#endif
      httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
      httpUpdate.rebootOnUpdate(false);

      t_httpUpdate_return result = httpUpdate.update(client, _firmwareUrl);

      switch (result)
      {
         case HTTP_UPDATE_FAILED:
            Serial.printf("OTAUpdater: update failed: %s\n", httpUpdate.getLastErrorString().c_str());
#ifdef ARDUINO_DISPLAY_SUPPORTED
            if (_arduino != nullptr)
            {
               _arduino->println();
               _arduino->println("Update failed", Color::RED);
               _arduino->println(httpUpdate.getLastErrorString().c_str(), Color::RED);
               delay(_RESULT_DELAY_MS);
            }
#endif
            break;

         case HTTP_UPDATE_NO_UPDATES:
            Serial.println("OTAUpdater: no update available");
#ifdef ARDUINO_DISPLAY_SUPPORTED
            if (_arduino != nullptr)
            {
               _arduino->println();
               _arduino->println("No update available", Color::RED);
               delay(_RESULT_DELAY_MS);
            }
#endif
            break;

         case HTTP_UPDATE_OK:
            Serial.println("OTAUpdater: update OK, restarting");
#ifdef ARDUINO_DISPLAY_SUPPORTED
            if (_arduino != nullptr)
            {
               _arduino->println("\n\nRestarting...", Color::LABEL);
            }
#endif
            ESP.restart();
            break;
      }
   }

public:
   ///
   /// <summary>
   /// Constructs an OTAUpdater, without a display (serial-only boards). The version-check
   /// URL is derived from firmwareUrl per convention: version.txt lives alongside the
   /// firmware binary in the same directory.
   /// </summary>
   /// <param name="version">This sketch's own version string (e.g. "v1.0").</param>
   /// <param name="firmwareUrl">URL of the firmware .bin to download when an update is available.</param>
   /// <param name="checkIntervalSecs">How often (in seconds) loop() checks for an update; 0 disables periodic checks (checkNow() still works).</param>
   ///
   OTAUpdater(const char* version, const char* firmwareUrl, float checkIntervalSecs = 0.0f)
      : _version(version), _versionUrl(_deriveVersionUrl(firmwareUrl)), _firmwareUrl(firmwareUrl), _checkTimer(checkIntervalSecs)
   {
   }

#ifdef ARDUINO_DISPLAY_SUPPORTED
   ///
   /// <summary>
   /// Constructs an OTAUpdater that shows download progress on the given display. The
   /// version-check URL is derived from firmwareUrl per convention: version.txt lives
   /// alongside the firmware binary in the same directory.
   /// </summary>
   /// <param name="version">This sketch's own version string (e.g. "v1.0").</param>
   /// <param name="firmwareUrl">URL of the firmware .bin to download when an update is available.</param>
   /// <param name="arduino">Display to show progress and results on while updating.</param>
   /// <param name="checkIntervalSecs">How often (in seconds) loop() checks for an update; 0 disables periodic checks (checkNow() still works).</param>
   ///
   OTAUpdater(const char* version, const char* firmwareUrl, ArduinoWithDisplay* arduino, float checkIntervalSecs = 0.0f)
      : _version(version), _versionUrl(_deriveVersionUrl(firmwareUrl)), _firmwareUrl(firmwareUrl), _checkTimer(checkIntervalSecs), _arduino(arduino)
   {
   }
#endif

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
