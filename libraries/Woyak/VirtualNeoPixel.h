#pragma once

#include "Led.h"
#include "Status.h"

///
/// <summary>
/// Diameter (in pixels) of the virtual NeoPixel circle drawn on the display.
/// </summary>
///
constexpr uint8_t VIRTUAL_NEOPIXEL_DIAMETER = 10;

///
/// <summary>
/// Margin (in pixels) between the virtual NeoPixel circle and the display's edges.
/// </summary>
///
constexpr uint8_t VIRTUAL_NEOPIXEL_MARGIN = 6;

///
/// <summary>
/// A virtual NeoPixel rendered as a small filled circle in the upper-right corner of
/// a display, standing in for a physical NeoPixel on boards that don't have one.
/// </summary>
///
class VirtualNeoPixelLED : public LED
{
private:
   LGFX* _display;
   int16_t _x = 0;
   int16_t _y = 0;
   uint8_t _red = 0;
   uint8_t _green = 0;
   uint8_t _blue = 0;

public:
   ///
   /// <summary>
   /// Constructs a virtual NeoPixel LED that draws onto the specified display.
   /// </summary>
   /// <param name="display">The display to draw the virtual NeoPixel on.</param>
   ///
   explicit VirtualNeoPixelLED(LGFX* display) : LED(0), _display(display)
   {
   }

   ///
   /// <summary>
   /// Positions the virtual NeoPixel in the upper-right corner of the display, draws
   /// its initial (off) state, and starts the background task that drives its
   /// blink/flash timing (inherited from LED), so callers don't need to pump loop().
   /// </summary>
   ///
   virtual void begin() override
   {
      LED::begin();

      _x = _display->width() - VIRTUAL_NEOPIXEL_MARGIN - VIRTUAL_NEOPIXEL_DIAMETER / 2;
      _y = VIRTUAL_NEOPIXEL_MARGIN + VIRTUAL_NEOPIXEL_DIAMETER / 2;
      _apply();
   }

   ///
   /// <summary>
   /// Redraws the circle using the current color, scaled by brightness/on-off state.
   /// </summary>
   ///
   virtual void _apply() override
   {
      uint8_t brightness = _isOn ? (uint8_t)(_level * _calibrationFactor) : 0;
      Color color = Color565::fromRGB(
         (uint16_t)_red * brightness / 255,
         (uint16_t)_green * brightness / 255,
         (uint16_t)_blue * brightness / 255);
      _display->fillCircle(_x, _y, VIRTUAL_NEOPIXEL_DIAMETER / 2, (uint16_t)color);
   }

   ///
   /// <summary>
   /// Sets the virtual NeoPixel color using normalized channel values.
   /// </summary>
   /// <param name="redLevel">Red channel intensity from 0.0 to 1.0</param>
   /// <param name="greenLevel">Green channel intensity from 0.0 to 1.0</param>
   /// <param name="blueLevel">Blue channel intensity from 0.0 to 1.0</param>
   ///
   void setColor(float redLevel, float greenLevel, float blueLevel)
   {
      _red = constrain(255 * redLevel, 0, 255);
      _green = constrain(255 * greenLevel, 0, 255);
      _blue = constrain(255 * blueLevel, 0, 255);
      _apply();
   }
};

///
/// <summary>
/// Uses a virtual (display-drawn) NeoPixel to indicate startup and connectivity
/// states, mirroring NeoPixelStatus's behavior for a real NeoPixel LED.
/// </summary>
///
class VirtualNeoPixelStatus : public IStatus
{
private:
   VirtualNeoPixelLED* _led;

public:
   ///
   /// <summary>
   /// Initializes the status indicator with a virtual NeoPixel LED.
   /// </summary>
   /// <param name="led">The virtual NeoPixel LED instance to use.</param>
   ///
   explicit VirtualNeoPixelStatus(VirtualNeoPixelLED* led) : _led(led)
   {
   }

   ///
   /// <summary>
   /// Initializes the virtual NeoPixel and applies the default brightness level.
   /// </summary>
   ///
   void begin() override
   {
      _led->begin();
      _led->setLevel(1.0f);
      setStatus(Status::STARTED);
   }

   ///
   /// <summary>
   /// Updates the virtual NeoPixel to represent the specified status.
   /// </summary>
   /// <param name="status">The status value to display.</param>
   ///
   void setStatus(Status status) override
   {
      // FAILED is applied immediately (rather than deferred to the next rising edge)
      // since it's normally followed by a blocking delay/reset; waiting for the next
      // blink transition could mean the color change never gets a chance to run.
      if (status == Status::FAILED)
      {
         _led->setColor(1.0f, 0.0f, 0.0f);
         _led->turnOnNow();
         return;
      }

      // Deferred to the next rising edge of any current blink cycle so a color
      // change (e.g. blue WIFI_CONNECTING -> green WEB_CONNECTING) doesn't cut the
      // LED's current on/off phase short.
      _led->runAtNextRisingEdge([this, status]()
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

         case Status::FAILED:
            // Handled above, outside the deferred action.
            break;
         }
      });
   }
};
