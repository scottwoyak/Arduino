#pragma once

#include <Arduino.h>
#include "Util.h"
#include <FastLed.h>

/// <summary>
/// Base class for programmable LEDs with on/off and blinking support.
/// </summary>
/// <remarks>
/// Provides core functionality for controlling LEDs including brightness levels
/// and blinking patterns. Subclasses handle ESP32 timer-based updates or FastLED integration.
/// </remarks>
class BasicLED
{
protected:
	uint8_t _pin;
	uint16_t _blinkIntervalMs = 0;
	uint8_t _level = 255;
	bool _isOn = false;
	unsigned long _blinkStart = 0;

	/// <summary>
	/// Applies the current LED state to the GPIO pin.
	/// </summary>
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
	/// <summary>
	/// Constructs a BasicLED on the specified pin.
	/// </summary>
	/// <param name="pin">GPIO pin number for the LED</param>
	BasicLED(uint8_t pin)
	{
		_pin = pin;
	}

	/// <summary>
	/// Destructor for BasicLED.
	/// </summary>
	virtual ~BasicLED() {}

	/// <summary>
	/// Initializes the LED pin for output.
	/// </summary>
	virtual void begin()
	{
		if (_pin > 0)
		{
			pinMode(_pin, OUTPUT);
			digitalWrite(_pin, LOW);
		}
	}

	/// <summary>
	/// Turns the LED on at its configured brightness level.
	/// </summary>
	virtual void turnOn()
	{
		_isOn = true;
		_blinkIntervalMs = 0; // no blinking
		_apply();
	}

	/// <summary>
	/// Turns the LED off.
	/// </summary>
	virtual void turnOff()
	{
		_isOn = false;
		_blinkIntervalMs = 0;
		_apply();
	}

	/// <summary>
	/// Sets the LED to blink with the specified interval.
	/// </summary>
	/// <param name="blinkIntervalMs">Half-period of the blink cycle in milliseconds</param>
	void blink(uint16_t blinkIntervalMs)
	{
		// no idea why this is needed, but without it, all the leds blink
		delayMicroseconds(50);

		_blinkStart = millis();
		_blinkIntervalMs = blinkIntervalMs;
	}

	/// <summary>
	/// Sets the LED brightness level as an integer (0-255).
	/// </summary>
	/// <param name="level">Brightness level from 0 (off) to 255 (full brightness)</param>
	virtual void setLevel(uint8_t level)
	{
		_level = constrain(level, 0, 255);
	}

	/// <summary>
	/// Sets the LED brightness level as a normalized float (0.0-1.0).
	/// </summary>
	/// <param name="level">Normalized brightness from 0.0 (off) to 1.0 (full)</param>
	virtual void setLevel(float level)
	{
		_level = constrain(255 * level, 0, 255);
	}

	/// <summary>
	/// Updates LED blinking state. Call periodically or override in subclass.
	/// </summary>
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

/// <summary>
/// LED controller using ESP32 software timer for automatic updates.
/// </summary>
/// <remarks>
/// Uses an ESP32 FreeRTOS timer to automatically manage blinking patterns.
/// User does not need to call loop() - updates happen automatically.
/// </remarks>
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
	/// <summary>
	/// Constructs an LED controller on the specified pin (default 0 = no pin).
	/// </summary>
	/// <param name="pin">GPIO pin number for the LED (0 = disabled)</param>
	LED(uint8_t pin=0) : BasicLED(pin)
	{}

	/// <summary>
	/// Initializes the LED and starts the internal timer for automatic updates.
	/// </summary>
	void begin()
	{
		BasicLED::begin();

		_startTimer();
	}
};

/// <summary>
/// RGB LED controller for tri-color LEDs using three GPIO pins.
/// </summary>
/// <remarks>
/// Manages red, green, and blue pins independently with normalized color mixing.
/// Brightness levels are automatically balanced to maintain consistent color intensity.
/// </remarks>
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
	/// <summary>
	/// Constructs an RGB LED controller with specified pins for R, G, B.
	/// </summary>
	/// <param name="redPin">GPIO pin for red channel</param>
	/// <param name="greenPin">GPIO pin for green channel</param>
	/// <param name="bluePin">GPIO pin for blue channel</param>
	RGBLED(uint8_t redPin, uint8_t greenPin, uint8_t bluePin) : _redPin(redPin), _greenPin(greenPin), _bluePin(bluePin)
	{}

	/// <summary>
	/// Initializes all three RGB pins for PWM output.
	/// </summary>
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

	/// <summary>
	/// Applies the current RGB color mix to the GPIO pins.
	/// </summary>
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

	/// <summary>
	/// Sets the RGB color using normalized channel values.
	/// </summary>
	/// <param name="redLevel">Red channel intensity from 0.0 to 1.0</param>
	/// <param name="greenLevel">Green channel intensity from 0.0 to 1.0</param>
	/// <param name="blueLevel">Blue channel intensity from 0.0 to 1.0</param>
	void setColor(float redLevel, float greenLevel, float blueLevel)
	{
		_redLevel = constrain(redLevel, 0.0f, 1.0f);
		_greenLevel = constrain(greenLevel, 0.0f, 1.0f);
		_blueLevel = constrain(blueLevel, 0.0f, 1.0f);
		_apply();
	}
};

/// <summary>
/// NeoPixel/WS2812B addressable LED controller using FastLED library.
/// </summary>
/// <remarks>
/// Manages a single NeoPixel LED with full RGB color control through the FastLED library.
/// Automatically detects and configures the appropriate pin and LED type for the platform.
/// </remarks>
constexpr uint8_t NUM_LEDS = 1;

class NeoPixelLED : public LED
{
private:
	CRGB _leds[NUM_LEDS];

public:
	/// <summary>
	/// Constructs a NeoPixelLED controller.
	/// </summary>
	NeoPixelLED() 
	{
		_leds[0] = CRGB::Black; // start with the led off
	}

	/// <summary>
	/// Initializes the FastLED library and configures the NeoPixel LED.
	/// </summary>
	virtual void begin() override
	{
		LED::begin();
#if defined ARDUINO_WAVESHARE_ESP32_S3_ZERO
		FastLED.addLeds<WS2812B, 21, RGB>(_leds, NUM_LEDS);
#else
		FastLED.addLeds<NEOPIXEL, PIN_NEOPIXEL>(_leds, NUM_LEDS);
#endif
	}

	/// <summary>
	/// Applies brightness and shows the current LED state.
	/// </summary>
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

	/// <summary>
	/// Sets the NeoPixel RGB color using normalized channel values.
	/// </summary>
	/// <param name="redLevel">Red channel intensity from 0.0 to 1.0</param>
	/// <param name="greenLevel">Green channel intensity from 0.0 to 1.0</param>
	/// <param name="blueLevel">Blue channel intensity from 0.0 to 1.0</param>
	void setColor(float redLevel, float greenLevel, float blueLevel)
	{
		uint8_t r = constrain(255 * redLevel, 0, 255);
		uint8_t g = constrain(255 * greenLevel, 0, 255);
		uint8_t b = constrain(255 * blueLevel, 0, 255);
		setColor(r, g, b);
	}

	/// <summary>
	/// Sets the NeoPixel RGB color using 8-bit channel values.
	/// </summary>
	/// <param name="r">Red channel value from 0 to 255</param>
	/// <param name="g">Green channel value from 0 to 255</param>
	/// <param name="b">Blue channel value from 0 to 255</param>
	void setColor(uint8_t r, uint8_t g, uint8_t b)
	{
		_leds[0] = CRGB(r,g,b);
		_apply();
	}
};
