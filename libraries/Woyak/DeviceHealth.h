#pragma once

#include <ArduinoJson.h>
#include <atomic>
#include <cfloat>
#include <cstdint>
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
/// "rssi" (dBm, max since the previous health report), "cpuMhz", "tempF" (degrees F, max since the previous health report),
/// "localTime" (e.g. "8:44 PM"; omitted until the clock has been synchronized),
/// "maxAllocHeap" (largest allocatable
/// block, bytes), "wifiReconnects" (number
/// of times WiFi has reconnected since begin() was called), "hubReconnects", and "telemetryReconnects".
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

   /// <summary>Interval between RSSI/temperature samples taken from recordLoop() (ms).</summary>
   static constexpr uint32_t SAMPLE_PERIOD_MS = 1000;

   /// <summary>Time of the last RSSI/temperature sample (ms).</summary>
   static inline uint32_t _lastSampleMs = 0;

   /// <summary>Highest RSSI since the last health report (dBm).</summary>
   static inline int32_t _maxRssi = INT32_MIN;

   /// <summary>Highest CPU temperature since the last health report (F).</summary>
   static inline float _maxTempF = -FLT_MAX;

   /// <summary>Takes an RSSI and CPU temperature sample and updates the running maximums.</summary>
   static void _sample()
   {
      int32_t rssi = WiFi.RSSI();
      if (rssi > _maxRssi)
      {
         _maxRssi = rssi;
      }

      float tempF = CPUTemp::readF();
      if (tempF > _maxTempF)
      {
         _maxTempF = tempF;
      }
   }

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
   /// Records a main-loop step; periodically samples RSSI and CPU temperature.
   /// </summary>
   ///
   static void recordLoop()
   {
      uint32_t now = millis();

      if (now - _lastSampleMs >= SAMPLE_PERIOD_MS)
      {
         _lastSampleMs = now;
         _sample();
      }
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
      _sample();
      (*doc)["rssi"] = _maxRssi;
      (*doc)["cpuMhz"] = ESP.getCpuFreqMHz();
      (*doc)["tempF"] = _maxTempF;
      (*doc)["uptimeS"] = millis() / 1000;
      (*doc)["freeHeap"] = ESP.getFreeHeap();
      (*doc)["maxAllocHeap"] = ESP.getMaxAllocHeap();
      (*doc)["wifiReconnects"] = _wifiReconnects.load();
      (*doc)["hubReconnects"] = hubReconnects.load();
      (*doc)["telemetryReconnects"] = telemetryReconnects.load();
      (*doc)["telemetryRate"] = telemetryRate;
      (*doc)["influxFailures"] = influxFailures.load();
      (*doc)["stackFree"] = uxTaskGetStackHighWaterMark(nullptr);
      _maxRssi = INT32_MIN;
      _maxTempF = -FLT_MAX;

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
