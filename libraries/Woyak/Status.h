#pragma once

#include "Led.h"

constexpr auto BLINK_INTERVAL_MS = 300;

enum Status
{
   NONE = 0,
   STARTED = 1,
   WIFI_CONNECTING = 2,
   WEB_CONNECTING = 3,
   READY = 4,
};

class IStatus
{
public:
   virtual void begin() = 0;
   virtual void setStatus(Status status) = 0;
};

class LedStatus : public IStatus
{
private:
   Led _powerLed;
   Led _wifiLed;
   Led _webLed;

public:
   LedStatus(uint8_t powerPin, uint8_t wifiPin, uint8_t webPin) : _powerLed(powerPin), _wifiLed(wifiPin), _webLed(webPin)
   {
      _webLed.setLevel(0.15f);
   }

   void begin() override
   {
      _powerLed.begin();
      _wifiLed.begin();
      _webLed.begin();
   }

   void setStatus(Status status) override
   {
      switch (status)
      {
      case Status::NONE:
         _powerLed.turnOff();
         _wifiLed.turnOff();
         _webLed.turnOff();
         break;

      case Status::STARTED:
         _powerLed.turnOn();
         _wifiLed.turnOff();
         _webLed.turnOff();
         break;

      case Status::WIFI_CONNECTING:
         _powerLed.turnOn();
         _wifiLed.blink(BLINK_INTERVAL_MS);
         _webLed.turnOff();
         break;

      case Status::WEB_CONNECTING:
         _powerLed.turnOn();
         _wifiLed.turnOn();
         _webLed.blink(BLINK_INTERVAL_MS);
         break;

      case Status::READY:
         _powerLed.turnOn();
         _wifiLed.turnOn();
         _webLed.turnOn();
         break;
      }

   }
};

class NeoLedStatus : public IStatus
{
private:
   NeoPixelLed _neo;

public:
   NeoLedStatus(uint8_t neoPin) : _neo(1, neoPin, NEO_RGB + NEO_KHZ800)
   {}

   void begin() override
   {
      _neo.begin();
      setStatus(Status::NONE);
   }

   void setStatus(Status status) override
   {
      switch (status)
      {
      case Status::NONE:
         _neo.setColor(255, 255, 255);
         _neo.turnOn();
         break;
      case Status::WIFI_CONNECTING:
         _neo.setColor(0, 0, 255);
         _neo.blink(BLINK_INTERVAL_MS);
         break;
      case Status::WEB_CONNECTING:
         _neo.setColor(0, 255, 0);
         _neo.blink(BLINK_INTERVAL_MS);
         break;
      case Status::READY:
         _neo.setColor(0, 255, 0);
         _neo.turnOn();
         break;
      }
   }
};
