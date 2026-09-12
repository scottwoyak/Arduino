#pragma once

#include <functional>
#include <string>
#include <vector>
#include <Arduino.h>
#include <esp_ota_ops.h>

#include "ColorX.h"
#include "ILogger.h"
#include "IPrinter.h"
#include "Logger.h"
#include "OTAUpdater.h"
#include "Rebooter.h"
#include "Status.h"
#include "TimeSync.h"
#include "Util.h"
#include "WiFiX.h"

///
/// <summary>
/// Base class for Arduino platforms. print()/println()/printlnR()/printHeader() are no-ops
/// by default; display-capable boards (see ArduinoWithDisplay) override them to render to
/// the display. Setup-time helpers below (beginInit(), initWifi(), initSensor(),
/// initClient()) explicitly echo their status text to Logger as well, so it's visible on
/// Serial regardless of whether the board has a display.
/// </summary>
///
class ArduinoBase : public IPrinter
{
private:
   /// <summary>WiFi connection manager, created on first initWifi() call.</summary>
   WiFiX* _wifiX = nullptr;

   /// <summary>Scheduled daily reboot handler; self-driving via an internal timer once begin() is called.</summary>
   Rebooter _rebooter;

protected:
   /// <summary>OTA firmware update handler; only created after enableOTA() is called.</summary>
   OTAUpdater* _ota = nullptr;

public:
   ///
   /// <summary>
   /// Initialize the Arduino platform.
   /// </summary>
   ///
   virtual void begin() = 0;

protected:
   ///
   /// <summary>
   /// Warns (via Serial) if the running firmware isn't booting from the partition a
   /// plain USB upload writes to, which means the device is running stale firmware left
   /// over from a previous OTA update rather than the sketch that was just uploaded.
   /// Most partition schemes reserve a "factory" slot that USB uploads always target, but
   /// schemes without one (e.g. "min_spiffs", which only defines "app0"/"app1") instead
   /// have USB uploads target the first OTA slot ("app0"). Call this once from begin().
   /// </summary>
   ///
   void _checkRunningPartition()
   {
      const esp_partition_t* running = esp_ota_get_running_partition();
      if (running == nullptr)
      {
         return;
      }

      const esp_partition_t* factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
      const esp_partition_t* expected = (factory != nullptr) ? factory : esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);

      if (expected != nullptr && running != expected)
      {
         Serial.printf("WARNING: running from partition '%s', not '%s' -- this may be stale OTA firmware, not your latest upload! Erase flash to fix.\n", running->label, expected->label);
      }
   }

public:

   ///
   /// <summary>
   /// No-op default; ArduinoWithDisplay overrides this to render to the display.
   /// </summary>
   /// <param name="str">The text to print.</param>
   /// <param name="textColor">Ignored; present only for IPrinter compatibility.</param>
   /// <param name="backgroundColor">Ignored; present only for IPrinter compatibility.</param>
   ///
   void print(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK) override
   {
   }

   ///
   /// <summary>
   /// No-op default; ArduinoWithDisplay overrides this to render to the display.
   /// </summary>
   /// <param name="str">The text to print (default: empty, i.e. just a newline).</param>
   /// <param name="textColor">Ignored; present only for IPrinter compatibility.</param>
   /// <param name="backgroundColor">Ignored; present only for IPrinter compatibility.</param>
   ///
   void println(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK) override
   {
   }

   ///
   /// <summary>
   /// Prints a newline with no text.
   /// </summary>
   ///
   void println()
   {
      println("");
   }

   ///
   /// <summary>
   /// No-op default; ArduinoWithDisplay overrides this to render to the display.
   /// </summary>
   /// <param name="str">The text to print.</param>
   /// <param name="textColor">Ignored; present only for IPrinter compatibility.</param>
   /// <param name="backgroundColor">Ignored; present only for IPrinter compatibility.</param>
   ///
   void printlnR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK) override
   {
   }

   ///
   /// <summary>
   /// Prints a header line (e.g. "Initializing" or "Updating Firmware") to Serial (via
   /// Logger) and, on display-capable boards, the display as well (see
   /// ArduinoWithDisplay::printHeader).
   /// </summary>
   /// <param name="str">The header text to print.</param>
   /// <param name="textColor">The text color.</param>
   ///
   void printHeader(const char* str, Color textColor = Color::HEADING) override
   {
      println(str, textColor);
      Logger::writeln(str);
   }

   ///
   /// <summary>
   /// Prints an "Initializing" header. Convenience wrapper for printHeader(), used at the
   /// start of setup() to standardize the initialization sequence across sketches.
   /// </summary>
   /// <param name="str">The header text to print.</param>
   ///
   void beginInit(const char* str = "Initializing")
   {
      printHeader(str);
   }

   ///
   /// <summary>
   /// Prints a "label: value" line, e.g. "Location: Studio", to Serial and, on display-
   /// capable boards, the display as well.
   /// </summary>
   /// <param name="label">The label text to print (e.g. "Location: ").</param>
   /// <param name="value">The value text to print right after the label.</param>
   ///
   void println(const char* label, const char* value)
   {
      print(label, Color::LABEL);
      printlnR(value, Color::VALUE);
      Logger::write(label);
      Logger::writeln(value);
   }

   ///
   /// <summary>
   /// Prints a "label: value" line for a numeric value, e.g. "Address: 68", to Serial and,
   /// on display-capable boards, the display as well.
   /// </summary>
   /// <param name="label">The label text to print (e.g. "Address: ").</param>
   /// <param name="value">The numeric value to print right after the label.</param>
   ///
   void println(const char* label, uint32_t value)
   {
      println(label, std::to_string(value).c_str());
   }

   ///
   /// <summary>
   /// Connects to WiFi via WiFiX (WiFiMulti-based), printing a "WiFi..." label and "OK"/"FAILED"
   /// based on the result, to Serial and, on display-capable boards, the display as well.
   /// Optionally drives an IStatus indicator through the WIFI_CONNECTING phase. Once connected,
   /// the status indicator (if provided) is advanced to WEB_CONNECTING to reflect the next
   /// initialization phase, and the system clock is synced via NTP (auto-detecting the local
   /// timezone), printing a "Time..." label with the resulting synchronized time; needed for
   /// calendar-based features like Rebooter's midnight reboot to fire at the correct wall-clock time.
   /// </summary>
   /// <param name="ssid">The WiFi network name.</param>
   /// <param name="password">The WiFi network password.</param>
   /// <param name="status">Optional status indicator updated to WIFI_CONNECTING while connecting, then WEB_CONNECTING once connected.</param>
   /// <param name="syncTime">True to sync the system clock via NTP after connecting.</param>
   /// <returns>True if the WiFi connection succeeded; otherwise false.</returns>
   ///
   bool initWifi(const char* ssid, const char* password, IStatus* status = nullptr, bool syncTime = true)
   {
      if (status != nullptr)
      {
         status->setStatus(Status::WIFI_CONNECTING);
      }

      print("WiFi...", Color::LABEL);
      Logger::write("WiFi...");

      if (_wifiX == nullptr)
      {
         _wifiX = new WiFiX(ssid, password);
      }

      if (!_wifiX->connect())
      {
         printlnR("FAILED", Color::RED);
         Logger::writeln("FAILED");

         std::string message = std::string("WiFi connect failed: ") + WiFiX::statusString();
         println(message.c_str(), Color::RED);
         Logger::writeln(message.c_str());
         return false;
      }

      printlnR(WiFi.localIP().toString().c_str(), Color::VALUE);
      Logger::writeln(WiFi.localIP().toString().c_str());

      if (status != nullptr)
      {
         status->setStatus(Status::WEB_CONNECTING);
      }

      if (syncTime)
      {
         print("Time...", Color::LABEL);
         Logger::write("Time...");
         TimeSync::syncWithAutoTimezone("pool.ntp.org", "time.nist.gov");

         Color timeColor = TimeSync::isSynced() ? Color::VALUE : Color::RED;
         printlnR(TimeSync::localTimeString().c_str(), timeColor);
         Logger::writeln(TimeSync::localTimeString().c_str());
      }

      return true;
   }

   ///
   /// <summary>
   /// Ensures WiFi is connected, reconnecting if needed, using the WiFiX instance created by
   /// initWifi(). Intended for periodic use from loop() to detect and recover from dropped
   /// connections. Optionally drives an IStatus indicator through the WIFI_CONNECTING/READY
   /// phases while reconnecting.
   /// </summary>
   /// <param name="status">Optional status indicator updated while reconnecting.</param>
   /// <returns>True when connected; otherwise false</returns>
   ///
   bool ensureWiFiConnected(IStatus* status = nullptr)
   {
      ASSERT(_wifiX != nullptr);

      // Only drive the status indicator through WIFI_CONNECTING/READY when a (re)connect is
      // actually needed. Calling setStatus() unconditionally every loop() iteration - even
      // when WiFi is already connected - hammers the NeoPixel driver (show() disables
      // interrupts while bit-banging) back-to-back with no throttling, which can starve other
      // tasks long enough to trip the interrupt/task watchdog and cause a panic reset.
      if (_wifiX->isConnected())
      {
         return true;
      }

      if (status != nullptr)
      {
         status->setStatus(Status::WIFI_CONNECTING);
      }

      bool isConnected = _wifiX->ensureConnected();

      if (status != nullptr && isConnected)
      {
         status->setStatus(Status::READY);
      }

      return isConnected;
   }

   ///
   /// <summary>
   /// Initializes a single sensor, printing a label and a success message (default "OK")
   /// or "NOT FOUND" to Serial and, on display-capable boards, the display as well.
   /// </summary>
   /// <param name="label">The sensor label to print (e.g. "Sensor 0 (New Surface)").</param>
   /// <param name="initFunc">Function that initializes the sensor and returns true on success.</param>
   /// <param name="successLabelFunc">Optional function called only on success to produce the
   /// success message (e.g. the detected sensor type) in place of "OK".</param>
   /// <returns>True if the sensor was found/initialized successfully.</returns>
   ///
   bool initSensor(const char* label, bool (*initFunc)(), const char* (*successLabelFunc)() = nullptr)
   {
      print(label, Color::LABEL);
      print("...", Color::LABEL);
      Logger::write(label);
      Logger::write("...");

      bool success = initFunc();
      if (success)
      {
         const char* successLabel = successLabelFunc != nullptr ? successLabelFunc() : "OK";
         printlnR(successLabel, Color::VALUE);
         Logger::writeln(successLabel);
      }
      else
      {
         printlnR("NOT FOUND", Color::RED);
         Logger::writeln("NOT FOUND");
      }
      return success;
   }

   ///
   /// <summary>
   /// Begins connecting to a client/service (e.g. a WebSocket or InfluxDB client), printing a
   /// label to Serial and, on display-capable boards, the display as well. The actual
   /// success/failure text is left to the sketch's existing async callback (e.g.
   /// onStarted/onConnected) since that completion is library-specific and asynchronous.
   /// Optionally drives an IStatus indicator through the WEB_CONNECTING phase.
   /// </summary>
   /// <param name="label">The client/service label to print (e.g. "WebSocket").</param>
   /// <param name="beginFunc">Function that starts the client/service connection. May be a
   /// capturing lambda or std::function (e.g. one bound to a member object).</param>
   /// <param name="status">Optional status indicator updated to WEB_CONNECTING while connecting.</param>
   ///
   void initClient(const char* label, std::function<void()> beginFunc, IStatus* status = nullptr)
   {
      if (status != nullptr)
      {
         status->setStatus(Status::WEB_CONNECTING);
      }

      print(label, Color::LABEL);
      print("...", Color::LABEL);
      Logger::write(label);
      Logger::write("...");
      beginFunc();
   }

   ///
   /// <summary>
   /// Records the current day as the reboot baseline and starts the scheduled daily
   /// reboot (via Rebooter), which self-drives via an internal timer. Call from
   /// setup(), after WiFi/time sync (e.g. after initWifi() or an InfluxDB begin() that
   /// syncs NTP).
   /// </summary>
   ///
   void enableRebooter()
   {
      _rebooter.begin();
   }

   ///
   /// <summary>
   /// Enables periodic (or on-demand) OTA firmware update checks and immediately performs
   /// one check (installing an update if available). The firmware and version-check URLs
   /// are both derived from sketchName per convention: this sketch publishes to a GitHub
   /// release tagged with its own name. WiFi must already be connected before calling this.
   /// On display-capable boards, use ArduinoWithDisplay::enableOTA() instead to also show
   /// download progress on the display.
   /// </summary>
   /// <param name="version">This sketch's own version string (e.g. "v1.0").</param>
   /// <param name="sketchName">This sketch's name (e.g. "Wind_Publisher"), used to derive its release URLs.</param>
   /// <param name="status">Optional status indicator set to FAILED if no OTA download partition is found.</param>
   /// <param name="checkIntervalSecs">How often (in seconds) loop() checks for an update; defaults to 10 minutes.</param>
   /// <param name="onUpdateAvailable">Optional handler, notified with the newly detected version string just before it's installed.</param>
   ///
   void enableOTA(const char* version, const char* sketchName, IStatus* status = nullptr, float checkIntervalSecs = OTAUpdater::DEFAULT_CHECK_INTERVAL_SECS, OTAUpdateEventHandler* onUpdateAvailable = nullptr)
   {
      _ota = new OTAUpdater(version, sketchName, checkIntervalSecs);
      if (onUpdateAvailable != nullptr)
      {
         _ota->setHandler(onUpdateAvailable);
      }
      _ota->setStatus(status);
      _ota->checkNow();
   }
   ///
   /// <summary>
   /// Drives the (if enabled) periodic OTA update check. Call once per loop() iteration.
   /// </summary>
   ///
   void checkForOTA()
   {
      if (_ota != nullptr)
      {
         _ota->loop();
      }
   }
};
