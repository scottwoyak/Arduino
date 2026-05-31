#pragma once

#include <LED.h>

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
   LED _powerLed;
   LED _wifiLed;
   LED _webLed;

public:
   LedStatus(uint8_t powerPin, uint8_t wifiPin, uint8_t webPin) : _powerLed(powerPin), _wifiLed(wifiPin), _webLed(webPin)
   {
	  // equalize the brightness of the leds, since they may be different colors
	  _powerLed.setLevel(0.30f); // white
	  _wifiLed.setLevel(1.0f);   // blue
	  _webLed.setLevel(0.20f);   // green
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

class RGBLEDStatus : public IStatus
{
private:
   RGBLED _led;

public:
   RGBLEDStatus(uint8_t redPin, uint8_t greenPin, uint8_t bluePin) : _led(redPin, greenPin, bluePin)
   {
   }

   void begin() override
   {
	  _led.begin();
	  setStatus(Status::NONE);
   }

   void setStatus(Status status) override
   {
	  switch (status)
	  {
	  case Status::NONE:
		 _led.turnOff();
		 break;

	  case Status::STARTED:
		 _led.setColor(1.0f, 1.0f, 1.0f);
		 _led.turnOn();
		 break;

	  case Status::WIFI_CONNECTING:
		 _led.setColor(0.0f, 0.0f, 1.0f);
		 _led.blink(BLINK_INTERVAL_MS);
		 break;

	  case Status::WEB_CONNECTING:
		 _led.setColor(0.0f, 1.0f, 0.0f);
		 _led.blink(BLINK_INTERVAL_MS);
		 break;

	  case Status::READY:
		 _led.setColor(0.0f, 1.0f, 0.0f);
		 _led.turnOn();
		 break;
	  }
   }
};

class NeoPixelStatus : public IStatus
{
private:
   bool _autoCreatedLED = false;
   NeoPixelLED* _led;

public:
   NeoPixelStatus(NeoPixelLED* led = nullptr)
   {
	  if (led != nullptr)
	  {
		 _led = led;
	  }
	  else
	  {

		 _led = new NeoPixelLED();
		 _autoCreatedLED = true;
	  }
   }

   ~NeoPixelStatus()
   {
	  if (_autoCreatedLED == true)
	  {
		 delete _led;
	  }
   }

   void begin() override
   {
	  if (_autoCreatedLED == true)
	  {
		 _led->begin();
	  }
	  setStatus(Status::NONE);
   }

   void setLevel(float level)
   {
	  _led->setLevel(level);
   }

   void setStatus(Status status) override
   {
	  switch (status)
	  {
	  case Status::NONE:
		 _led->turnOff();
		 break;

	  case Status::STARTED:
		 _led->setColor(1.0f, 1.0f, 1.0f);
		 _led->turnOn();
		 break;

	  case Status::WIFI_CONNECTING:
		 _led->setColor(0.0f, 0.0f, 1.0f);
		 _led->blink(BLINK_INTERVAL_MS);
		 break;

	  case Status::WEB_CONNECTING:
		 _led->setColor(0.0f, 1.0f, 0.0f);
		 _led->blink(BLINK_INTERVAL_MS);
		 break;

	  case Status::READY:
		 _led->setColor(0.0f, 1.0f, 0.0f);
		 _led->turnOn();
		 break;
	  }
   }
};
