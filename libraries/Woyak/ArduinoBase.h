#pragma once

#include <string>
#include <Arduino.h>

#include "Color.h"
#include "IPrinter.h"
#include "Rebooter.h"
#include "Status.h"
#include "Util.h"
#include "WiFiX.h"

///
/// <summary>
/// Base class for Arduino platforms. Implements IPrinter with a Serial-only default so any
/// board (with or without a display) can use the shared init/status helpers below; display-
/// capable boards (see ArduinoWithDisplay) override the print/println/printlnR/printHeader
/// methods to render to their display as well.
/// </summary>
///
class ArduinoBase : public IPrinter
{
private:
   /// <summary>WiFi connection manager, created on first initWifi() call.</summary>
   WiFiX* _wifiX = nullptr;

   /// <summary>Scheduled daily reboot handler; self-driving via an internal timer once begin() is called.</summary>
   Rebooter _rebooter;

public:
   ///
   /// <summary>
   /// Initialize the Arduino platform.
   /// </summary>
   ///
   virtual void begin() = 0;

   ///
   /// <summary>
   /// Prints text to Serial without a trailing newline.
   /// </summary>
   /// <param name="str">The text to print.</param>
   /// <param name="textColor">Ignored; present only for IPrinter compatibility.</param>
   /// <param name="backgroundColor">Ignored; present only for IPrinter compatibility.</param>
   ///
   void print(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK) override
   {
      Serial.print(str);
   }

   ///
   /// <summary>
   /// Prints text to Serial followed by a newline.
   /// </summary>
   /// <param name="str">The text to print.</param>
   /// <param name="textColor">Ignored; present only for IPrinter compatibility.</param>
   /// <param name="backgroundColor">Ignored; present only for IPrinter compatibility.</param>
   ///
   void println(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK) override
   {
      Serial.println(str);
   }

   ///
   /// <summary>
   /// Prints text to Serial followed by a newline. Since Serial output has no concept of
   /// cursor position, this behaves identically to println().
   /// </summary>
   /// <param name="str">The text to print.</param>
   /// <param name="textColor">Ignored; present only for IPrinter compatibility.</param>
   /// <param name="backgroundColor">Ignored; present only for IPrinter compatibility.</param>
   ///
   void printlnR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK) override
   {
      Serial.println(str);
   }

   ///
   /// <summary>
   /// Prints the sketch's initialization header (e.g. "Initializing") to Serial and, on
   /// display-capable boards, the display as well (see ArduinoWithDisplay::printHeader).
   /// </summary>
   /// <param name="str">The header text to print.</param>
   /// <param name="textColor">The text color.</param>
   ///
   void printHeader(const char* str, Color textColor = Color::HEADING) override
   {
      Serial.println(str);
      println(str, textColor);
   }

   ///
   /// <summary>
   /// Prints an "Initializing" header to Serial and, on display-capable boards, the
   /// display as well. Convenience wrapper for printHeader(), used at the start of
   /// setup() to standardize the initialization sequence across sketches.
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
      Serial.print(label);
      Serial.println(value);

      print(label, Color::LABEL);
      printlnR(value, Color::VALUE);
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
   /// Optionally drives an IStatus indicator through the WIFI_CONNECTING phase.
   /// </summary>
   /// <param name="ssid">The WiFi network name.</param>
   /// <param name="password">The WiFi network password.</param>
   /// <param name="status">Optional status indicator updated to WIFI_CONNECTING while connecting.</param>
   /// <returns>True if the WiFi connection succeeded; otherwise false.</returns>
   ///
   bool initWifi(const char* ssid, const char* password, IStatus* status = nullptr)
   {
      if (status != nullptr)
      {
         status->setStatus(Status::WIFI_CONNECTING);
      }

      print("WiFi...", Color::LABEL);

      if (_wifiX == nullptr)
      {
         _wifiX = new WiFiX(ssid, password);
      }

      if (_wifiX->connect())
      {
         printlnR(WiFi.localIP().toString().c_str(), Color::VALUE);
         return true;
      }
      else
      {
         printlnR("FAILED", Color::RED);

         std::string message = std::string("WiFi connect failed: ") + WiFiX::statusString();
         println(message.c_str(), Color::RED);
         return false;
      }
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
      // when WiFi is already connected - hammers the NeoPixel driver (FastLED.show() disables
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
   /// Initializes a single sensor, printing a label and "OK"/"NOT FOUND
   /// to Serial and, on display-capable boards, the display as well.
   /// </summary>
   /// <param name="label">The sensor label to print (e.g. "Sensor 0 (New Surface)").</param>
   /// <param name="initFunc">Function that initializes the sensor and returns true on success.</param>
   /// <returns>True if the sensor was found/initialized successfully.</returns>
   ///
   bool initSensor(const char* label, bool (*initFunc)())
   {
      print(label, Color::LABEL);
      print("...", Color::LABEL);

      bool success = initFunc();
      if (success)
      {
         printlnR("OK", Color::VALUE);
      }
      else
      {
         printlnR("NOT FOUND", Color::RED);
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
   /// <param name="beginFunc">Function that starts the client/service connection.</param>
   /// <param name="status">Optional status indicator updated to WEB_CONNECTING while connecting.</param>
   ///
   void initClient(const char* label, void (*beginFunc)(), IStatus* status = nullptr)
   {
      if (status != nullptr)
      {
         status->setStatus(Status::WEB_CONNECTING);
      }

      print(label, Color::LABEL);
      print("...", Color::LABEL);
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
};
