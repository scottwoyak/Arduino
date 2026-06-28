#pragma once

#include <Arduino.h>
#include <WiFi.h>

/// <summary>
/// Simple WiFi utility methods for connecting and reconnecting.
/// </summary>
class WiFiX
{
public:
   /// <summary>
   /// Default WiFi connection timeout in milliseconds.
   /// </summary>
   static constexpr uint32_t DEFAULT_TIMEOUT_MS = 10000;

private:
   /// <summary>
   /// Delay between connection checks in milliseconds.
   /// </summary>
   static constexpr uint32_t CHECK_INTERVAL_MS = 100;

public:

   /// <summary>
   /// Starts a station-mode WiFi connection attempt.
   /// </summary>
   /// <param name="wifiSSID">WiFi network SSID</param>
   /// <param name="wifiPassword">WiFi network password</param>
   /// <returns>None</returns>
   static void begin(const char* wifiSSID, const char* wifiPassword)
   {
      WiFi.mode(WIFI_STA);
      WiFi.reconnect();
      WiFi.begin(wifiSSID, wifiPassword);
   }

   /// <summary>
   /// Connects to WiFi and waits for completion.
   /// </summary>
   /// <param name="wifiSSID">WiFi network SSID</param>
   /// <param name="wifiPassword">WiFi network password</param>
   /// <param name="timeoutMs">Maximum time to wait for connection in milliseconds</param>
   /// <returns>True when connected; otherwise false</returns>
   static bool connect(
      const char* wifiSSID,
      const char* wifiPassword,
      uint32_t timeoutMs = DEFAULT_TIMEOUT_MS)
   {
      if (WiFi.status() == WL_CONNECTED)
      {
         return true;
      }

      begin(wifiSSID, wifiPassword);

      const uint32_t start = millis();
      while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs)
      {
         Serial.print(".");
         delay(CHECK_INTERVAL_MS);
      }

      return WiFi.status() == WL_CONNECTED;
   }

   /// <summary>
   /// Ensures WiFi is connected, reconnecting if needed.
   /// </summary>
   /// <param name="wifiSSID">WiFi network SSID</param>
   /// <param name="wifiPassword">WiFi network password</param>
   /// <param name="timeoutMs">Maximum time to wait for reconnection in milliseconds</param>
   /// <returns>True when connected; otherwise false</returns>
   static bool ensureConnected(
      const char* wifiSSID,
      const char* wifiPassword,
      uint32_t timeoutMs = DEFAULT_TIMEOUT_MS)
   {
      if (WiFi.status() == WL_CONNECTED)
      {
         return true;
      }

      return connect(wifiSSID, wifiPassword, timeoutMs);
   }
};
