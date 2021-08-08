
// our passwords not under version control
#include "WiFiSettings.h"

#include <SolarTHPWeatherStation.h>

// pins
const uint8_t BATTERY_VOLTS_PIN = PIN_A3;
const uint8_t BATTERY_CHARGING_VOLTS_PIN = PIN_A7;

SolarTHPWeatherStation weatherStation("cabin", BATTERY_VOLTS_PIN, BATTERY_CHARGING_VOLTS_PIN);

void setup() {
   weatherStation.setup();
}

void loop() {
   weatherStation.loop();
}
