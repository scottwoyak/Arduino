/// <summary>
/// RGB LED status indicator demonstration.
/// </summary>
/// <remarks>
/// Cycles through various system status states and displays them using RGB LED colors.
/// Demonstrates the Status enum and RGBLEDStatus class for visual system state indication.
/// 
/// States tested:
/// - NONE: Off
/// - STARTED: Initializing
/// - WIFI_CONNECTING: Connecting to WiFi
/// - WEB_CONNECTING: Connecting to web service
/// - READY: Operating normally
/// 
/// Hardware: ESP32 with common-cathode RGB LED on PWM pins.
/// </remarks>

#include <Arduino.h>

#include "Status.h"

constexpr uint8_t RED_PIN = 1;
constexpr uint8_t GREEN_PIN = 2;
constexpr uint8_t BLUE_PIN = 3;

constexpr unsigned long INTERVAL_OFF_MS = 1000;
constexpr unsigned long INTERVAL_STARTED_MS = 1000;
constexpr unsigned long INTERVAL_WIFI_CONNECTING_MS = 2000;
constexpr unsigned long INTERVAL_WEB_CONNECTING_MS = 2000;
constexpr unsigned long INTERVAL_READY_MS = 5000;

RGBLEDStatus status(RED_PIN, GREEN_PIN, BLUE_PIN);

void setup()
{
   status.begin();
}

void loop()
{
   status.setStatus(Status::NONE);
   delay(INTERVAL_OFF_MS);

   status.setStatus(Status::STARTED);
   delay(INTERVAL_STARTED_MS);

   status.setStatus(Status::WIFI_CONNECTING);
   delay(INTERVAL_WIFI_CONNECTING_MS);

   status.setStatus(Status::WEB_CONNECTING);
   delay(INTERVAL_WEB_CONNECTING_MS);

   status.setStatus(Status::READY);
   delay(INTERVAL_READY_MS);
}
