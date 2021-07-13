#pragma once

#include <AdafruitIO.h>
#include <Arduino.h>

class ILogHandler {
public:
  virtual void print(const String &) = 0;
};

class SerialLogHandler : ILogHandler {
public:
  void print(const String &str) { Serial.print(str); }
};

class FeedLogHandler : ILogHandler {
private:
  AdafruitIO_Feed *_feed;

public:
  FeedLogHandler(AdafruitIO_Feed *feed) { this->_feed = feed; }

  void print(const String &str) { this->_feed->save(str); }
};

#define MAX_HANDLERS 5
class Logger {
private:
  ILogHandler *_handlers[MAX_HANDLERS] = {nullptr, nullptr, nullptr, nullptr,
                                          nullptr};

public:
  void print(const String &str) {
    for (int i = 0; i < MAX_HANDLERS; i++) {
      if (this->_handlers[i] != nullptr) {
        this->_handlers[i]->print(str);
      }
    }
  }
  void println(const String &str) { this->print(str + '\n'); }
};
