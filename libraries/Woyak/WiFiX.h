#pragma once

#include <Arduino.h>
#include <WiFiMulti.h>

///
/// <summary>
/// WiFi utility for connecting and reconnecting to a single access point using WiFiMulti.
/// </summary>
///
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

   /// <summary>Underlying WiFiMulti instance used for connection management.</summary>
   WiFiMulti _wifiMulti;

   /// <summary>Registered access point SSID.</summary>
   const char* _wifiSSID;

   /// <summary>Registered access point password.</summary>
   const char* _wifiPassword;

   /// <summary>True after WiFi mode and AP have been configured on first connect.</summary>
   bool _initialized = false;

   /// <summary>
   /// Returns a human-readable label for a WiFi status code.
   /// </summary>
   /// <param name="status">wl_status_t value to describe</param>
   /// <returns>Status description string</returns>
   static const char* _statusString(uint8_t status)
   {
      switch (status)
      {
         case WL_IDLE_STATUS:     return "idle";
         case WL_NO_SSID_AVAIL:   return "SSID not found";
         case WL_SCAN_COMPLETED:  return "scan completed";
         case WL_CONNECTED:       return "connected";
         case WL_CONNECT_FAILED:  return "connect failed";
         case WL_CONNECTION_LOST: return "connection lost";
         case WL_DISCONNECTED:    return "disconnected";
         default:                 return "unknown";
      }
   }

public:

   /// <summary>
   /// Returns a human-readable string for the current WiFi connection status.
   /// </summary>
   /// <returns>Status description string</returns>
   static const char* statusString()
   {
      return _statusString(WiFi.status());
   }

   /// <summary>
   /// Creates a WiFiX instance and registers the access point credentials.
   /// </summary>
   /// <param name="wifiSSID">WiFi network SSID</param>
   /// <param name="wifiPassword">WiFi network password</param>
   WiFiX(const char* wifiSSID, const char* wifiPassword)
   {
      _wifiSSID = wifiSSID;
      _wifiPassword = wifiPassword;
   }

   /// <summary>
   /// Connects to WiFi and waits for completion.
   /// </summary>
   /// <param name="timeoutMs">Maximum time to wait for connection in milliseconds</param>
   /// <returns>True when connected; otherwise false</returns>
   bool connect(uint32_t timeoutMs = DEFAULT_TIMEOUT_MS)
   {
      if (WiFi.status() == WL_CONNECTED)
      {
         return true;
      }

      if (!_initialized)
      {
         WiFi.mode(WIFI_STA);
         _wifiMulti.addAP(_wifiSSID, _wifiPassword);
         _initialized = true;
      }

      const uint32_t start = millis();
      while (_wifiMulti.run() != WL_CONNECTED && (millis() - start) < timeoutMs)
      {
         Serial.print(".");
         delay(CHECK_INTERVAL_MS);
      }

      return WiFi.status() == WL_CONNECTED;
   }

   /// <summary>
   /// Ensures WiFi is connected, reconnecting if needed.
   /// </summary>
   /// <param name="timeoutMs">Maximum time to wait for reconnection in milliseconds</param>
   /// <returns>True when connected; otherwise false</returns>
   bool ensureConnected(uint32_t timeoutMs = DEFAULT_TIMEOUT_MS)
   {
      if (WiFi.status() == WL_CONNECTED)
      {
         return true;
      }

      return connect(timeoutMs);
   }

   /// <summary>
   /// Forces a full WiFi disconnect and reconnect, even if currently connected.
   /// This clears stale DHCP/DNS state that a simple status check would not detect,
   /// which is useful when the network link is up but requests to a remote host are
   /// being refused (e.g. due to a stale cached DNS resolution).
   /// </summary>
   /// <param name="timeoutMs">Maximum time to wait for reconnection in milliseconds</param>
   /// <returns>True when reconnected; otherwise false</returns>
   bool reconnect(uint32_t timeoutMs = DEFAULT_TIMEOUT_MS)
   {
      WiFi.disconnect();

      const uint32_t start = millis();
      while (_wifiMulti.run() != WL_CONNECTED && (millis() - start) < timeoutMs)
      {
         Serial.print(".");
         delay(CHECK_INTERVAL_MS);
      }

      return WiFi.status() == WL_CONNECTED;
   }
};
