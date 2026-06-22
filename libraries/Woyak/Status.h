#pragma once

#include <Color.h>
#include <LED.h>

/// <summary>
/// Blink interval used for animated status indicators.
/// </summary>
constexpr auto BLINK_INTERVAL_MS = 300;

//-------------------------------------------------------------------------------------------------
//
// Identifies the current device startup or connectivity state.
//
//-------------------------------------------------------------------------------------------------
/// <summary>
/// Identifies the current device startup or connectivity state.
/// </summary>
enum Status
{
	NONE = 0,
	STARTED = 1,
	WIFI_CONNECTING = 2,
	WEB_CONNECTING = 3,
	READY = 4,
};

//-------------------------------------------------------------------------------------------------
//
// Defines a common interface for status indicators used during startup and connectivity changes.
//
//-------------------------------------------------------------------------------------------------
/// <summary>
/// Defines a common interface for status indicators used during startup and connectivity changes.
/// </summary>
class IStatus
{
public:
	virtual ~IStatus() = default;

	/// <summary>
	/// Initializes the status indicator.
	/// </summary>
	virtual void begin() = 0;

	/// <summary>
	/// Updates the indicator to reflect the specified status.
	/// </summary>
	/// <param name="status">The status value to display.</param>
	virtual void setStatus(Status status) = 0;
};

//-------------------------------------------------------------------------------------------------
//
// Drives three discrete LEDs to represent power, WiFi, and web connectivity states.
//
//-------------------------------------------------------------------------------------------------
/// <summary>
/// Drives three discrete LEDs to represent power, WiFi, and web connectivity states.
/// </summary>
class LedStatus : public IStatus
{
private:
	LED _powerLed;
	LED _wifiLed;
	LED _webLed;

public:
	/// <summary>
	/// Initializes the status indicator with dedicated power, WiFi, and web LEDs.
	/// </summary>
	/// <param name="powerPin">The pin used for the power status LED.</param>
	/// <param name="wifiPin">The pin used for the WiFi status LED.</param>
	/// <param name="webPin">The pin used for the web status LED.</param>
	LedStatus(uint8_t powerPin, uint8_t wifiPin, uint8_t webPin)
		: _powerLed(powerPin), _wifiLed(wifiPin), _webLed(webPin)
	{
		_powerLed.setLevel(0.30f);
		_wifiLed.setLevel(1.0f);
		_webLed.setLevel(0.20f);
	}

	/// <summary>
	/// Initializes all three status LEDs.
	/// </summary>
	void begin() override
	{
		_powerLed.begin();
		_wifiLed.begin();
		_webLed.begin();
	}

	/// <summary>
	/// Updates the discrete LEDs to represent the specified status.
	/// </summary>
	/// <param name="status">The status value to display.</param>
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

//-------------------------------------------------------------------------------------------------
//
// Uses a single RGB LED to indicate startup and connectivity states with different colors.
//
//-------------------------------------------------------------------------------------------------
/// <summary>
/// Uses a single RGB LED to indicate startup and connectivity states with different colors.
/// </summary>
class RGBLEDStatus : public IStatus
{
private:
	RGBLED _led;

public:
	/// <summary>
	/// Initializes the status indicator with a single RGB LED.
	/// </summary>
	/// <param name="redPin">The red channel pin.</param>
	/// <param name="greenPin">The green channel pin.</param>
	/// <param name="bluePin">The blue channel pin.</param>
	RGBLEDStatus(uint8_t redPin, uint8_t greenPin, uint8_t bluePin)
		: _led(redPin, greenPin, bluePin)
	{
	}

	/// <summary>
	/// Initializes the RGB LED and clears the current status.
	/// </summary>
	void begin() override
	{
		_led.begin();
		setStatus(Status::NONE);
	}

	/// <summary>
	/// Updates the RGB LED to represent the specified status.
	/// </summary>
	/// <param name="status">The status value to display.</param>
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

//-------------------------------------------------------------------------------------------------
//
// Uses a NeoPixel LED to indicate startup and connectivity states with built-in color helpers.
//
//-------------------------------------------------------------------------------------------------
/// <summary>
/// Uses a NeoPixel LED to indicate startup and connectivity states with built-in color helpers.
/// </summary>
class NeoPixelStatus : public IStatus
{
private:
	bool _autoCreatedLED = false;
	NeoPixelLED* _led;

public:
	/// <summary>
	/// Initializes the status indicator with a NeoPixel LED.
	/// </summary>
	/// <param name="led">The NeoPixel LED instance to use, or null to create one internally.</param>
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

	~NeoPixelStatus() override
	{
		if (_autoCreatedLED)
		{
			delete _led;
		}
	}

	/// <summary>
	/// Initializes the NeoPixel using the default brightness level.
	/// </summary>
	void begin() override
	{
		begin(0.05f);
	}

	/// <summary>
	/// Initializes the NeoPixel LED and applies the specified brightness level.
	/// </summary>
	/// <param name="level">The NeoPixel brightness level from 0.0 to 1.0.</param>
	void begin(float level)
	{
		if (_autoCreatedLED)
		{
			_led->begin();
		}

		_led->setLevel(level);
		setStatus(Status::NONE);
	}

	/// <summary>
	/// Updates the active NeoPixel brightness level.
	/// </summary>
	/// <param name="level">The NeoPixel brightness level from 0.0 to 1.0.</param>
	void setLevel(float level)
	{
		_led->setLevel(level);
	}

	/// <summary>
	/// Updates the NeoPixel to represent the specified status.
	/// </summary>
	/// <param name="status">The status value to display.</param>
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

	/// <summary>
	/// Sets a custom NeoPixel status color using normalized RGB channel values.
	/// </summary>
	/// <param name="r">The red channel level from 0.0 to 1.0.</param>
	/// <param name="g">The green channel level from 0.0 to 1.0.</param>
	/// <param name="b">The blue channel level from 0.0 to 1.0.</param>
	void setStatus(float r, float g, float b)
	{
		_led->setColor(r, g, b);
		_led->turnOn();
	}

	/// <summary>
	/// Sets a custom NeoPixel status color from a 16-bit display color value.
	/// </summary>
	/// <param name="color">The color to apply to the NeoPixel.</param>
	void setStatus(Color color)
	{
		_led->setColor(Color565::getR(color), Color565::getG(color), Color565::getB(color));
		_led->turnOn();
	}
};
