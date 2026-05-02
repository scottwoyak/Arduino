#pragma once

#include <Arduino.h>
#include <Latch.h>
#include <Util.h>
#include <Adafruit_NeoPixel.h>

//
// This class manages a LED where the user turns it on and off
//
class BasicLed
{
private:
   uint8_t _pin;
   uint8_t _blinkIntervalMs = 0;
   uint8_t _level = 255;
   bool _isOn = false;

public:
   BasicLed(uint8_t pin)
   {
      _pin = pin;
   }

   virtual void begin()
   {
      if (_pin > 0)
      {
         pinMode(_pin, OUTPUT);
         digitalWrite(_pin, LOW);
      }
   }

   virtual bool isOn()
   {
      return _isOn;
   }

   virtual void turnOn()
   {
      analogWrite(_pin, _level);
      _isOn = _level > 0;
   }

   virtual void turnOff()
   {
      analogWrite(_pin, 0);
      _isOn = false;
   }

   void setBlinkInterval(uint16_t blinkIntervalMs)
   {
      _blinkIntervalMs = blinkIntervalMs;
   }

   virtual void setLevel(uint8_t level)
   {
      _level = constrain(level, 0, 255);
      analogWrite(this->_pin, _level);
   }

   virtual void setLevel(float level)
   {
      _level = constrain(255 * level, 0, 255);
      analogWrite(_pin, _level);
   }

   virtual void loop()
   {
      if (_blinkIntervalMs > 0)
      {
         if (millis() % (2 * _blinkIntervalMs) < _blinkIntervalMs)
         {
            turnOn();
         }
         else
         {
            turnOff();
         }
      }
   }
};

//
// This class uses an ESP32 software timer to manage the led. The user doesn't have to call loop()
//
class Led : public BasicLed
{
private:
   TimerHandle_t _timerHandle = nullptr;

   static void _timerCallback(TimerHandle_t xTimer)
   {
      Led* led = static_cast<Led*>(pvTimerGetTimerID(xTimer));
      led->loop();
   }

public:
   Led(uint8_t pin) : BasicLed(pin)
   {
   }

   void begin()
   {
      BasicLed::begin();

      _timerHandle = xTimerCreate("BlinkTimer", pdMS_TO_TICKS(1), pdTRUE, this, _timerCallback);

      if (_timerHandle != nullptr)
      {
         xTimerStart(_timerHandle, 0); // Start the timer
      }
   }
};

class NeoPixelLed : public Led
{
private:
   Adafruit_NeoPixel _pixels;

public:
   NeoPixelLed(uint16_t numPixels, int8_t pin, neoPixelType type) : Led(0), _pixels(numPixels, pin, type)
   {
   }

   virtual void begin()
   {
      _pixels.begin();
      _pixels.show(); // Initialize all pixels to 'off'
   }

   virtual void setLevel(uint8_t level)
   {
      _pixels.setBrightness(level);
   }

   virtual void setLevel(float level)
   {
      _pixels.setBrightness(constrain(255 * level, 0, 255));
      _pixels.show();
   }

   virtual void turnOn()
   {
      _pixels.setPixelColor(0, _pixels.getPixelColor(0));
      _pixels.show();
   }

   virtual void turnOff()
   {
      _pixels.setPixelColor(0, 0, 0, 0);
      _pixels.show();
   }

   void setColor(uint8_t r, uint8_t g, uint8_t b)
   {
      _pixels.setPixelColor(0, r, g, b);
      _pixels.show();
   }
};
