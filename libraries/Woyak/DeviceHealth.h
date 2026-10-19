#pragma once

#include <ArduinoJson.h>
#include <atomic>
#include <cfloat>
#include <cstdint>
#include <WiFi.h>
#include <cmath>
#include <time.h>

#include "ArduinoBoardId.h"
#ifdef ARDUINO_BATTERY_SUPPORTED
#include "Battery.h"
#endif
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
/// "rssi" (dBm, average since the previous health report), "cpuMhz", "tempF" (degrees F, max since the previous health report),
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

   /// <summary>Sum of RSSI samples since the last health report (dBm).</summary>
   static inline int64_t _rssiSum = 0;

   /// <summary>Number of RSSI samples accumulated since the last health report.</summary>
   static inline uint32_t _rssiCount = 0;

   /// <summary>Most recent RSSI sample (dBm), independent of the since-last-report average above; see lastRssi().</summary>
   static inline int32_t _lastRssi = 0;

   /// <summary>Highest CPU temperature since the last health report (F).</summary>
   static inline float _maxTempF = -FLT_MAX;

   /// <summary>Takes an RSSI and CPU temperature sample and updates the running accumulators.</summary>
   static void _sample()
   {
      int32_t rssi = WiFi.RSSI();
      _lastRssi = rssi;
      _rssiSum += rssi;
      _rssiCount++;

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

   /// <summary>millis() when telemetryRate was last updated; the rate is reported as 0 once it goes stale.</summary>
   static inline volatile uint32_t telemetryRateMs = 0;

   /// <summary>Milliseconds without a telemetry sample after which the reported rate is 0.</summary>
   static constexpr uint32_t TELEMETRY_RATE_STALE_MS = 5000;

   /// <summary>Failed InfluxDB writes since boot.</summary>
   static inline std::atomic<uint32_t> influxFailures{ 0 };

#ifdef ARDUINO_BATTERY_SUPPORTED
   /// <summary>Optional board Battery instance, wired in by SketchBase on boards with battery support.</summary>
   static inline Battery* battery = nullptr;
#endif

   /// <summary>Optional sketch-supplied callback that adds extra sketch-specific values (e.g. sensor readings) to the health JSON in fill().</summary>
   static inline void (*extraFields)(JsonDocument* doc) = nullptr;

   ///
   /// <summary>
   /// Most recent RSSI sample (dBm), updated every SAMPLE_PERIOD_MS by recordLoop().
   /// Safe to call from any context (e.g. a shell command handler) since it just returns
   /// a cached value instead of querying the WiFi driver synchronously, which has been
   /// observed to briefly disrupt the WiFi connection.
   /// </summary>
   /// <returns>The most recent RSSI sample, in dBm.</returns>
   ///
   static int32_t lastRssi() { return _lastRssi; }

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
      (*doc)["rssi"] = (int32_t)lround((double)_rssiSum / _rssiCount);
      (*doc)["cpuMhz"] = ESP.getCpuFreqMHz();
      (*doc)["tempF"] = _maxTempF;
      (*doc)["uptimeS"] = millis() / 1000;
      (*doc)["freeHeap"] = ESP.getFreeHeap();
      (*doc)["maxAllocHeap"] = ESP.getMaxAllocHeap();
      (*doc)["wifiReconnects"] = _wifiReconnects.load();
      (*doc)["hubReconnects"] = hubReconnects.load();
      (*doc)["telemetryReconnects"] = telemetryReconnects.load();
      (*doc)["telemetryRate"] = (millis() - telemetryRateMs > TELEMETRY_RATE_STALE_MS) ? 0.0f : telemetryRate;
      (*doc)["influxFailures"] = influxFailures.load();
      (*doc)["stackFree"] = uxTaskGetStackHighWaterMark(nullptr);
      _rssiSum = 0;
      _rssiCount = 0;
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

                         #ifdef ARDUINO_BATTERY_SUPPORTED
                               if (battery != nullptr)
                               {
                                  bool connected = battery->isConnected();
                                  BatteryChargingState chargingState = battery->readChargingState();
                                  (*doc)["batteryConnected"] = connected;

                                  if (chargingState == BatteryChargingState::BadCharger)
                                  {
                                     (*doc)["batteryVolts"] = roundf(battery->readVolts() * 100.0f) * 0.01f;
                                     (*doc)["batteryState"] = "Bad Charger";
                                  }
                                  else if (connected)
                                  {
                                     (*doc)["batteryVolts"] = roundf(battery->readVolts() * 100.0f) * 0.01f;

                                     float percent = battery->readPercent();
                                     if (isfinite(percent))
                                     {
                                        (*doc)["batteryPercent"] = (int32_t)lroundf(percent);
                                     }

                                     if (chargingState == BatteryChargingState::Yes)
                                     {
                                        (*doc)["batteryCharging"] = true;
                                        (*doc)["batteryState"] = "Charging";
                                     }
                                     else if (chargingState == BatteryChargingState::No)
                                     {
                                        (*doc)["batteryCharging"] = false;
                                        (*doc)["batteryState"] = "Discharging";
                                     }
                                     else
                                     {
                                        (*doc)["batteryState"] = "Unknown";
                                     }

                                     float chargeRate = battery->readChargeRate();
                                     if (isfinite(chargeRate))
                                     {
                                        (*doc)["batteryChargeRate"] = roundf(chargeRate * 10.0f) * 0.1f;
                                     }
                                  }
                               }
                         #endif

                                if (extraFields != nullptr)
      {
         extraFields(doc);
      }
   }
};
