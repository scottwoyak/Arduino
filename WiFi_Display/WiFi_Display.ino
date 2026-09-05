//
// WiFi signal strength scanner display.
//
// Continuously scans for nearby WiFi networks (without connecting to any of them) and
// shows the top 2 strongest signals, sorted by RSSI. If an access point broadcasts more
// than one SSID, both are combined into a single, comma-separated entry rather than
// being shown as two separate signals. Each entry shows the SSID(s) on its own line,
// followed by a larger line with the RSSI and band (2.4G/5G).
//
// Pressing buttonA attempts to connect to the standard WiFi network, showing progress
// and the result. Once connected, the sketch stops scanning and instead continually
// shows the live signal strength of the connected network.
//

#include <algorithm>
#include <cstring>
#include <vector>

#include <Arduino.h>
#include <WiFi.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_BUTTON_A_SUPPORTED
#error "This sketch requires a board with buttonA support (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "SerialX.h"
#include "WiFiSettings.h"

// ----------- The Board
Arduino arduino;

// ----------- Mode
enum class Mode
{
   SCANNING,
   CONNECTED,
};
Mode mode = Mode::SCANNING;

// ----------- Text Sizes
constexpr uint8_t HEADER_SIZE = 3;
constexpr uint8_t ROW_SIZE = 2;
constexpr uint8_t DETAIL_SIZE = ROW_SIZE + 1;

// ----------- Scan Results
constexpr uint8_t NUM_TOP_NETWORKS = 2;

// ----------- Display Items
constexpr size_t SSID_WIDTH = 28;
constexpr size_t BAND_WIDTH = 6;
constexpr float HEADER_GAP_RATIO = 1.2f;
constexpr int16_t ENTRY_GAP_PX = 4;
Format ssidFormat(SSID_WIDTH, Format::Alignment::LEFT);
Format rssiFormat("#### dBm", Format::Alignment::LEFT);
Format bandFormat(BAND_WIDTH, Format::Alignment::LEFT);

///
/// <summary>
/// Determines the WiFi band ("2.4G" or "5G") from a channel number.
/// </summary>
/// <param name="channel">The channel number reported by WiFi.channel().</param>
/// <returns>"2.4G" for channels 1-14, "5G" otherwise.</returns>
///
const char* bandLabel(int32_t channel)
{
   return channel <= 14 ? "2.4G" : "5G";
}

///
/// <summary>
/// Determines whether two BSSIDs (MAC addresses) belong to the same physical access
/// point, based on sharing the same last 5 octets, which commonly stay fixed while
/// only the first octet differs between virtual SSIDs on the same radio.
/// </summary>
/// <param name="bssidA">The first BSSID, as returned by WiFi.BSSID().</param>
/// <param name="bssidB">The second BSSID, as returned by WiFi.BSSID().</param>
/// <returns>True if both BSSIDs belong to the same access point.</returns>
///
bool sameAccessPoint(const uint8_t* bssidA, const uint8_t* bssidB)
{
   for (uint8_t i = 1; i < 6; i++)
   {
      if (bssidA[i] != bssidB[i])
      {
         return false;
      }
   }

   return true;
}

///
/// <summary>
/// A scanned access point, with its SSID(s), RSSI, band, and BSSID captured at scan
/// time so the rest of the sketch doesn't need to repeatedly query WiFi scan results.
/// </summary>
///
struct NetworkInfo
{
   String ssids;
   int32_t rssi;
   const char* band;
   uint8_t bssid[6];

   ///
   /// <summary>
   /// Captures the SSID, RSSI, band, and BSSID for a single scan result.
   /// </summary>
   /// <param name="scanIndex">Index of the network in the WiFi scan results.</param>
   /// <param name="scanBssid">The network's BSSID, as returned by WiFi.BSSID().</param>
   ///
   NetworkInfo(int16_t scanIndex, const uint8_t* scanBssid)
   {
      ssids = WiFi.SSID(scanIndex);
      rssi = WiFi.RSSI(scanIndex);
      band = bandLabel(WiFi.channel(scanIndex));
      memcpy(bssid, scanBssid, sizeof(bssid));
   }
};

void setup()
{
   SerialX::begin();

   arduino.begin();

   WiFi.mode(WIFI_STA);
   WiFi.disconnect();

   arduino.clearDisplay();
   arduino.setTextSize(HEADER_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("WiFi Scan", Color::HEADING);
}

///
/// <summary>
/// Attempts to connect to the standard WiFi network, showing progress and the result
/// on the display. On success, switches the sketch to CONNECTED mode.
/// </summary>
///
void connectToWifi()
{
   arduino.clearDisplay();
   arduino.setTextSize(HEADER_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("Connecting", Color::HEADING);
   arduino.setTextSize(ROW_SIZE);

   if (arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, nullptr, false))
   {
      mode = Mode::CONNECTED;

      arduino.clearDisplay();
      arduino.setTextSize(HEADER_SIZE);
      arduino.setCursor(0, 0);
      arduino.println("WiFi Signal", Color::HEADING);
   }
}

///
/// <summary>
/// Displays the live signal strength (SSID, RSSI, and band) of the currently
/// connected WiFi network.
/// </summary>
///
void showConnectedSignal()
{
   arduino.setTextSize(ROW_SIZE);
   arduino.setCursorY(HEADER_GAP_RATIO * arduino.charH(HEADER_SIZE));
   arduino.setCursorX(0);

   arduino.println(WiFi.SSID().c_str(), ssidFormat, Color::LABEL);

   arduino.setTextSize(DETAIL_SIZE);
   arduino.setCursorX(0);
   arduino.print(WiFi.RSSI(), rssiFormat, Color::VALUE);
   arduino.println(bandLabel(WiFi.channel()), bandFormat, Color::VALUE);
   arduino.setTextSize(ROW_SIZE);
}

void loop()
{
   if (arduino.buttonA.wasPressed() && mode == Mode::SCANNING)
   {
      connectToWifi();
      return;
   }

   if (mode == Mode::CONNECTED)
   {
      showConnectedSignal();
      return;
   }

   int16_t count = WiFi.scanNetworks();

   // Build a list of unique access points, appending to an existing entry's SSIDs if a
   // duplicate SSID is broadcast by an access point already in the list, otherwise
   // creating a new entry.
   std::vector<NetworkInfo> networks;
   for (int16_t i = 0; i < count; i++)
   {
      uint8_t* bssid = WiFi.BSSID(i);

      bool merged = false;
      for (NetworkInfo& network : networks)
      {
         if (sameAccessPoint(network.bssid, bssid))
         {
            network.ssids += ", " + WiFi.SSID(i);
            merged = true;
            break;
         }
      }

      if (!merged)
      {
         networks.push_back(NetworkInfo(i, bssid));
      }
   }

   std::sort(networks.begin(), networks.end(), [](const NetworkInfo& a, const NetworkInfo& b)
   {
      return a.rssi > b.rssi;
   });

   arduino.setTextSize(ROW_SIZE);
   arduino.setCursorY(HEADER_GAP_RATIO * arduino.charH(HEADER_SIZE));

   for (uint8_t i = 0; i < NUM_TOP_NETWORKS; i++)
   {
      arduino.setCursorX(0);
      if (i < networks.size())
      {
         const NetworkInfo& network = networks[i];

         arduino.println(network.ssids.c_str(), ssidFormat, Color::LABEL);

         arduino.setTextSize(DETAIL_SIZE);
         arduino.setCursorX(0);
         arduino.print(network.rssi, rssiFormat, Color::VALUE);
         arduino.println(network.band, bandFormat, Color::VALUE);
         arduino.setTextSize(ROW_SIZE);
         arduino.moveCursorY(ENTRY_GAP_PX);
      }
      else
      {
         arduino.println();
         arduino.println();
      }
   }

   WiFi.scanDelete();
}
