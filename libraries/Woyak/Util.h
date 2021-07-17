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
         // wait 10 seconds for connection:
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
};