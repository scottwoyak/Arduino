#pragma once

#include <AdafruitIO.h>
#include <Arduino.h>

class FlashErrorMsg {
private:
public:
  void save(const char *msg, AdafruitIO_Feed *feed = nullptr) {
    Serial.println(msg);

    if (feed) {
      feed->save(msg);
    }

    // this->writeStringToEEPROM(0, msg);
  }

  void sendToFeedIfExists(AdafruitIO_Feed *feed) {
    String str;
    // this->readStringFromEEPROM(0, &str);

    if (str.length() > 0) {
      String msg("Flash Error: ");
      msg += str;
      Serial.println(msg);
    }
  }

  void printLnIfExists() {
    String str;
    // this->readStringFromEEPROM(0, &str);

    if (str.length() > 0) {
      Serial.print("Flash Error: ");
      Serial.println(str);
    }
  }

  bool exists() {
    String str;
    // this->readStringFromEEPROM(0, &str);
    return (str.length() > 0);
  }

  void clear() {
    // this->writeStringToEEPROM(0, "");
  }
};
