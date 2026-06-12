#pragma once

#include <Arduino.h>
#include <Util.h>
#include <FastLed.h>

// TODO - standardize when changes are applied - e.g. right now color is immediate but level is not

//
// This class manages a LED that is programmatically turned on and off
//
class BasicLED
{
protected:
   uint8_t _pin;
   uint16_t _blinkIntervalMs = 0;
   uint8_t _level = 255;
   bool _isOn = false;
   unsigned long _blinkStart = 0;

   virtual void _apply()
   {
      if (_isOn)
      {
         analogWrite(_pin, _level);
      }
      else
      {
         analogWrite(_pin, 0);
      }
   }

public:
   BasicLED(uint8_t pin)
   {
      _pin = pin;
   }

   virtual ~BasicLED() {}

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
      _isOn = true;
      _blinkIntervalMs = 0; // no blinking
      _apply();
   }

   virtual void turnOff()
   {
      _isOn = false;
      _blinkIntervalMs = 0;
      _apply();
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
		 bool newIsOn = ((millis() - _blinkStart) % (2 * _blinkIntervalMs) < _blinkIntervalMs);
		 if (newIsOn != _isOn)
		 {
			_isOn = newIsOn;
			_apply();
		 }
      }
   }
};

//
// This class uses an ESP32 software timer to manage the led. The user doesn't have to call loop()
//
class LED : public BasicLED
{
private:

   static void _timerCallback(TimerHandle_t xTimer)
   {
      LED* led = static_cast<LED*>(pvTimerGetTimerID(xTimer));
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
   LED(uint8_t pin=0) : BasicLED(pin)
   {}

   void begin()
   {
      BasicLED::begin();

      _startTimer();
   }
};

class RGBLED : public LED
{
private:
   uint8_t _redPin;
   uint8_t _greenPin;
   uint8_t _bluePin;

   float _redLevel = 1.0f;
   float _greenLevel = 1.0f;
   float _blueLevel = 1.0f;

public:
   RGBLED(uint8_t redPin, uint8_t greenPin, uint8_t bluePin) : _redPin(redPin), _greenPin(greenPin), _bluePin(bluePin)
   {}

   virtual void begin() override
   {
      LED::begin();
      pinMode(_redPin, OUTPUT);
      digitalWrite(_redPin, LOW);
      pinMode(_greenPin, OUTPUT);
      digitalWrite(_greenPin, LOW);
      pinMode(_bluePin, OUTPUT);
      digitalWrite(_bluePin, LOW);
   }

   virtual void _apply() override
   {
      if (_isOn)
      {
         // normalize rgb levels to total 1
         float r = _redLevel / (_redLevel + _greenLevel + _blueLevel);
         float g = _greenLevel / (_redLevel + _greenLevel + _blueLevel);
         float b = _blueLevel / (_redLevel + _greenLevel + _blueLevel);

         analogWrite(_redPin, _level * r);
         analogWrite(_greenPin, _level * g);
         analogWrite(_bluePin, _level * b);
      }
      else
      {
         analogWrite(_redPin, 0);
         analogWrite(_greenPin, 0);
         analogWrite(_bluePin, 0);
      }
   }

   void setColor(float redLevel, float greenLevel, float blueLevel)
   {
      _redLevel = constrain(redLevel, 0.0f, 1.0f);
      _greenLevel = constrain(greenLevel, 0.0f, 1.0f);
      _blueLevel = constrain(blueLevel, 0.0f, 1.0f);
      _apply();
   }
};

constexpr uint8_t NUM_LEDS = 1;

class NeoPixelLED : public LED
{
private:
   CRGB _leds[NUM_LEDS];

public:
   NeoPixelLED() 
   {
	  _leds[0] = CRGB::Black; // start with the led off
   }

   virtual void begin() override
   {
      LED::begin();
#if defined ARDUINO_WAVESHARE_ESP32_S3_ZERO
      FastLED.addLeds<WS2812B, 21, RGB>(_leds, NUM_LEDS);
#else
      FastLED.addLeds<NEOPIXEL, PIN_NEOPIXEL>(_leds, NUM_LEDS);
#endif
   }

   virtual void _apply() override
   {
      if (_isOn)
      {
         FastLED.setBrightness(_level);
      }
      else
      {
         FastLED.setBrightness(0);
      }
      FastLED.show();
   }

   void setColor(float redLevel, float greenLevel, float blueLevel)
   {
	  uint8_t r = constrain(255 * redLevel, 0, 255);
	  uint8_t g = constrain(255 * greenLevel, 0, 255);
	  uint8_t b = constrain(255 * blueLevel, 0, 255);
	  setColor(r, g, b);
   }

   void setColor(uint8_t r, uint8_t g, uint8_t b)
   {
      _leds[0] = CRGB(r,g,b);
      _apply();
   }
};
