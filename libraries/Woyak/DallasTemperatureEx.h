#pragma once

#include <DallasTemperature.h>

class OneWire;

class DallasTemperatureEx : public DallasTemperature {
public:
  DallasTemperatureEx(OneWire *oneWire) : DallasTemperature(oneWire) {}

  void printAddresses() {
    int deviceCount = this->getDeviceCount();
    DeviceAddress address;
    for (int i = 0; i < deviceCount; i++) {
      this->getAddress(address, i);
      printAddress(address);
      Serial.println();
    }
  }

  // function to print a device address
  void printAddress(DeviceAddress deviceAddress) {
    Serial.print("{");
    for (uint8_t i = 0; i < 8; i++) {
      Serial.print(deviceAddress[i]);
      if (i < 7) {
        Serial.print(",");
      }
    }
    Serial.print("}");
  }
};