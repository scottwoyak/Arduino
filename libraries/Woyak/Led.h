#pragma once

#include <Arduino.h>
#include <Latch.h>
#include <Util.h>
#include <Adafruit_NeoPixel.h>

//
// This class manages a LED that is programmatically turned on and off
//
class BasicLed
{
protected:
   uint8_t _pin;
   uint16_t _blinkIntervalMs = 0;
   uint8_t _level = 255;
   unsigned long _blinkStart = 0;

   virtual void _on()
   {
      analogWrite(_pin, _level);
   }

   virtual void _off()
   {
      analogWrite(_pin, 0);
   }

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

   virtual void turnOn()
   {
      //Serial.println("----- Turning On " + String(_pin));
      _blinkIntervalMs = 0; // no blinking
      _on();
   }

   virtual void turnOff()
   {
      //Serial.println("----- Turning Off " + String(_pin));
      _blinkIntervalMs = 0;
      _off();
   }

   void blink(uint16_t blinkIntervalMs)
   {
      // no idea why this is needed, but without it, all the leds blink
      delayMicroseconds(50);

      _blinkStart = millis();
      _blinkIntervalMs = blinkIntervalMs;
   }

   virtual void setLevel(uint8_t level)
   {
      _level = constrain(level, 0, 255);
   }

   virtual void setLevel(float level)
   {
      _level = constrain(255 * level, 0, 255);
   }

   virtual void loop()
   {
      if (_blinkIntervalMs > 0)
      {
         if ((millis() - _blinkStart) % (2 * _blinkIntervalMs) < _blinkIntervalMs)
         {
            _on();
         }
         else
         {
            _off();
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

   static void _timerCallback(TimerHandle_t xTimer)
   {
      Led* led = static_cast<Led*>(pvTimerGetTimerID(xTimer));
      led->loop();
   }

   void _startTimer()
   {
      TimerHandle_t timerHandle = xTimerCreate(
         "BlinkTimer",     // only used for debugging
         pdMS_TO_TICKS(1), // tick interval in ms
         pdTRUE,           // auto-reload
         this,             // user data
         _timerCallback);  // callback function

      if (timerHandle != nullptr)
      {
         xTimerStart(timerHandle, 0); // Start the timer
      }
   }

public:
   Led(uint8_t pin) : BasicLed(pin)
   {}

   void begin()
   {
      BasicLed::begin();

      _startTimer();
   }
};

// TODO blinking doesn't work with NeoPixels. When _pixels.show() is called, it kills the timer
class NeoPixelLed : public Led
{
private:
   Adafruit_NeoPixel _pixels;

public:
   NeoPixelLed(uint16_t numPixels, int8_t pin, neoPixelType type) : Led(0), _pixels(numPixels, pin, type)
   {}

   virtual void begin()
   {
      Led::begin();

      _pixels.begin();
      _pixels.setBrightness(0);
      _pixels.show();
   }

   virtual void _on() override
   {
      _pixels.setBrightness(_level);
      _pixels.show();
   }

   virtual void _off() override
   {
      _pixels.setBrightness(0);
      _pixels.show();
   }

   void setColor(uint8_t r, uint8_t g, uint8_t b)
   {
      _pixels.setPixelColor(0, r, g, b);
   }
};
