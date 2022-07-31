#pragma once

#include <AdafruitIO.h>
#include <Adafruit_GrayOLED.h>
#include <Arduino.h>

class ILogHandler {
public:
   virtual void print(const String&) = 0;
};

class SerialLogHandler : public ILogHandler {
public:
   void print(const String& str) {
      Serial.print(str);
   }
};

class FeedLogHandler : public ILogHandler {
private:
   AdafruitIO_Feed* _feed;

public:
   FeedLogHandler(AdafruitIO_Feed* feed) {
      this->_feed = feed;
   }

   void print(const String& str) {
      this->_feed->save(str);
   }
};

class DisplayLogHandler : public ILogHandler {
private:
   Adafruit_GrayOLED* _display;

public:
   DisplayLogHandler(Adafruit_GrayOLED* display) {
      this->_display = display;
   }

   void print(const String& str) {
      this->_display->print(str);
      this->_display->display();
   }
};

#define MAX_HANDLERS 5
class Logger {
private:
   ILogHandler* _handlers[MAX_HANDLERS] = { nullptr, nullptr, nullptr, nullptr,
                                           nullptr };

public:
   Logger(
      ILogHandler* h1 = nullptr,
      ILogHandler* h2 = nullptr,
      ILogHandler* h3 = nullptr,
      ILogHandler* h4 = nullptr,
      ILogHandler* h5 = nullptr
   )
   {
      this->_handlers[0] = h1;
      this->_handlers[1] = h2;
      this->_handlers[2] = h3;
      this->_handlers[3] = h4;
      this->_handlers[4] = h5;
   };

   void print(const String& str) {
      for (int i = 0; i < MAX_HANDLERS; i++) {
         if (this->_handlers[i] != nullptr) {
            this->_handlers[i]->print(str);
         }
      }
   }
   void println(const String& str) {
      this->print(str + '\n');
   }
};
