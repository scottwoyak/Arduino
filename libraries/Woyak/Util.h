#pragma once

#include "WiFi101.h"
#include <AdafruitIO.h>
#include <Stopwatch.h>

namespace Util {

   bool connectToWifi(const char* ssid, const char* pass, ulong timeoutMs = 0) {
      Serial.print("Connecting to ");
      Serial.print(ssid);

      WiFi.setPins(8, 7, 4, 2);
      int status = WiFi.begin(ssid, pass);

      Stopwatch sw;
      while (status != WL_CONNECTED) {

         if (timeoutMs > 0 && sw.elapsedMillis() > timeoutMs) {
            break;
         }

         // wait for connection:
         Serial.print(".");
         delay(500);
      }
      Serial.println();

      if (status == WL_CONNECTED) {
         Serial.print("Connected to ");
         Serial.println(ssid);
         return true;
      }
      else {
         Serial.println("Connection failed");
         return false;
      }
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

   float readVolts(uint8_t pin, uint16_t resolution = 4096, float voltageDivider = 2) {
      float volts = analogRead(pin);
      volts *= voltageDivider;    // compensate for voltage divider
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