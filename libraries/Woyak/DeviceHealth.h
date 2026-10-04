#pragma once

#include <ArduinoJson.h>
#include <atomic>
#include <WiFi.h>
#include <time.h>

#include "CPUTemp.h"
#include "Units.h"
#include "Util.h"

///
/// <summary>
/// Collects device health values (WiFi signal, CPU frequency, CPU temperature, uptime,
/// free heap, local time) and writes them into a JSON object, for reporting to the
/// DeviceServer in reply to a "GetHealth" command (see DeviceServerClient).
/// </summary>
/// <remarks>
/// JSON keys: "rssi" (dBm), "cpuMhz", "tempF" (degrees F), "uptimeS", "freeHeap" (bytes), and
/// "localTime" (e.g. "8:44 PM"; omitted until the clock has been synchronized),
/// "minFreeHeap" (lowest free heap since boot, bytes), "maxAllocHeap" (largest allocatable
/// block, bytes), "wifiReconnects" (number
/// of times WiFi has reconnected since begin() was called), "hubReconnects", "telemetryReconnects",
/// "telemetryRate", "influxFailures", "stackFree" (min free stack, bytes), and "maxLoopMs".
/// </remarks>
///
class DeviceHealth
{
   /// <summary>Epoch times below this mean the clock hasn't been synchronized (matches TimeSync::isSynced()).</summary>
   static constexpr time_t MIN_SYNCED_EPOCH = 1000000000L;

   /// <summary>Number of WiFi reconnects since begin().</summary>
   static inline std::atomic<uint32_t> _wifiReconnects{ 0 };

   /// <summary>WiFi event handler; every GOT_IP seen after begin() is a reconnect, since the initial connection precedes begin().</summary>
   static void _onWiFiGotIp(arduino_event_id_t event, arduino_event_info_t info)
   {
      _wifiReconnects++;
   }

   /// <summary>Longest gap between loop steps since boot (ms).</summary>
   static inline uint32_t _maxLoopMs = 0;

   /// <summary>Time of the previous loop step (ms).</summary>
   static inline uint32_t _lastLoopMs = 0;

public:
   /// <summary>Hub disconnects since boot (after the first successful connection).</summary>
   static inline std::atomic<uint32_t> hubReconnects{ 0 };

   /// <summary>Telemetry disconnects since boot (after a successful connection).</summary>
   static inline std::atomic<uint32_t> telemetryReconnects = 0;

   /// <summary>Most recent telemetry sample rate (samples/s).</summary>
   static inline volatile float telemetryRate = 0;

   /// <summary>Failed InfluxDB writes since boot.</summary>
   static inline std::atomic<uint32_t> influxFailures{ 0 };

   /// <summary>Optional sketch-supplied callback that adds extra sketch-specific values (e.g. sensor readings) to the health JSON in fill().</summary>
   static inline void (*extraFields)(JsonDocument* doc) = nullptr;

   ///
   /// <summary>
   /// Records a main-loop step; tracks the longest gap between steps.
   /// </summary>
   ///
   static void recordLoop()
   {
      uint32_t now = millis();
      if (_lastLoopMs != 0 && now - _lastLoopMs > _maxLoopMs)
      {
         _maxLoopMs = now - _lastLoopMs;
      }
      _lastLoopMs = now;
   }

   ///
   /// <summary>
   /// Starts counting
   /// </summary>
   ///
   static void begin()
   {
      WiFi.onEvent(_onWiFiGotIp, ARDUINO_EVENT_WIFI_STA_GOT_IP);
      CPUTemp::begin();
   }

   ///
   /// <summary>
   /// Adds the current health values to the given JSON document.
   /// </summary>
   /// <param name="doc">Document to populate; existing keys with the same names are replaced.</param>
   ///
   static void fill(JsonDocument* doc)
   {
      (*doc)["rssi"] = WiFi.RSSI();
      (*doc)["cpuMhz"] = ESP.getCpuFreqMHz();
      (*doc)["tempF"] = CPUTemp::readF();
      (*doc)["uptimeS"] = millis() / 1000;
      (*doc)["freeHeap"] = ESP.getFreeHeap();
      (*doc)["minFreeHeap"] = ESP.getMinFreeHeap();
      (*doc)["maxAllocHeap"] = ESP.getMaxAllocHeap();
      (*doc)["wifiReconnects"] = _wifiReconnects.load();
      (*doc)["hubReconnects"] = hubReconnects.load();
      (*doc)["telemetryReconnects"] = telemetryReconnects.load();
      (*doc)["telemetryRate"] = telemetryRate;
      (*doc)["influxFailures"] = influxFailures.load();
      (*doc)["stackFree"] = uxTaskGetStackHighWaterMark(nullptr);
      (*doc)["maxLoopMs"] = _maxLoopMs;

      time_t now = time(nullptr);
      if (now >= MIN_SYNCED_EPOCH)
      {
         struct tm timeInfo;
         localtime_r(&now, &timeInfo);

         char buffer[16];
         strftime(buffer, sizeof(buffer), "%I:%M %p", &timeInfo);

         // Strip a leading zero from the hour, e.g. "09:45 AM" -> "9:45 AM".
         (*doc)["localTime"] = (buffer[0] == '0') ? buffer + 1 : buffer;
      }

      if (extraFields != nullptr)
      {
         extraFields(doc);
      }
   }
};
