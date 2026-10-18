//
// NeoPixel status indicator demonstration.
//
// Cycles through various system status states and displays them using a NeoPixel LED.
// Demonstrates the Status enum and NeoPixelStatus class for visual system state indication.
//
// States tested:
// - off(): Off
// - STARTED: Initializing
// - WIFI_CONNECTING: Connecting to WiFi
// - SERVICES_CONNECTING: Connecting to web services
// - RUNNING: Operating normally
//
// Hardware: ESP32 with a WS2812B/NeoPixel LED on GPIO 10.
//

#include <Arduino.h>

#include "Status.h"

constexpr int16_t NEOPIXEL_PIN = 10;

constexpr unsigned long INTERVAL_OFF_MS = 1000;
constexpr unsigned long INTERVAL_STARTED_MS = 1000;
constexpr unsigned long INTERVAL_WIFI_CONNECTING_MS = 2000;
constexpr unsigned long INTERVAL_SERVICES_CONNECTING_MS = 2000;
constexpr unsigned long INTERVAL_READY_MS = 5000;

NeoPixelStatus status(NEOPIXEL_PIN);

void setup()
{
   status.begin();
}

void loop()
{
   status.off();
   delay(INTERVAL_OFF_MS);

   status.setStatus(Status::STARTED);
   delay(INTERVAL_STARTED_MS);

   status.setStatus(Status::WIFI_CONNECTING);
   delay(INTERVAL_WIFI_CONNECTING_MS);

   status.setStatus(Status::SERVICES_CONNECTING);
   delay(INTERVAL_SERVICES_CONNECTING_MS);

   status.setStatus(Status::RUNNING);
   delay(INTERVAL_READY_MS);
}
