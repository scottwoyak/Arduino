#pragma once

#include "WiFi101.h"
#include <AdafruitIO.h>

namespace Util {

   void connectToWifi(const char* ssid, const char* pass) {
      Serial.print("Attempting to connect to ");
      Serial.print(ssid);

      WiFi.setPins(8, 7, 4, 2);
      int status = WiFi.begin(ssid, pass);

      while (status != WL_CONNECTED) {
         // wait for connection:
         Serial.print(".");
         delay(500);
      }
      Serial.println();

      Serial.print("Connected to ");
      Serial.println(ssid);
   }

   void connectToAdafruitIO(AdafruitIO* io) {
      Serial.print("Connecting to Adafruit IO");
      io->connect();

      // wait for a connection
      while (io->status() < AIO_CONNECTED) {
         Serial.print(".");
         delay(500);
      }
      Serial.println();
   }

   float readVolts(uint8_t pin, uint16_t resolution = 4096) {
      float volts = analogRead(pin);
      volts *= 2;    // we divided by 2 with 100k resistors, so multiply back
      volts *= 3.3;  // Multiply by 3.3V, our reference voltage
      volts /= resolution; // convert to voltage
      return volts;
   }

   float voltsToPercent(float volts, float fullChargeVolts = 4.2) {
      const float MIN_VOLTS = 3.5;
      float percent = 100 * (volts - MIN_VOLTS) / (fullChargeVolts - MIN_VOLTS);
      percent = constrain(percent, 0, 100);
      return percent;
   }

   float C2F(float celsius) {
      return 32 + (9.0 / 5.0) * celsius;
   }

   float F2C(float fahrenheit) {
      return (fahrenheit - 32) * (5.0 / 9.0);
   }
};