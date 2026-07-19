#pragma once

#include <Arduino.h>
#include "IEncoder.h"
#include <limits>

// Modulino.h transitively includes Arduino_LTR381RGB.h, which declares a global "RGB"
// object (LTR381RGBClass RGB). FastLED's eorder.h separately imports "using fl::RGB;"
// into the global namespace, and if both end up in the same translation unit (e.g. via
// ArduinoBoard.h), the compiler reports "RGB redeclared as different kind of entity".
// Renaming the Modulino-side symbol via a macro for the duration of this include avoids
// the collision without requiring a separate translation unit.
#define RGB ModulinoLTR381RGB_RGB
#include <Modulino.h>
#undef RGB

///
/// <summary>
/// Modulino Knob (I2C rotary encoder module) backend implementation.
/// </summary>
/// <remarks>
/// Wraps a ModulinoKnob module, which is polled over I2C rather than driven by GPIO
/// interrupts. Position and button state are read on demand rather than tracked
/// asynchronously, so getPosition()/isButtonPressed() incur an I2C transaction.
/// </remarks>
///
class ModulinoEncoder : public IEncoder
{
private:
   // ModulinoKnob::get()/isPressed() aren't const, but reading the current position
   // and button state doesn't logically mutate this encoder's observable state.
   mutable ModulinoKnob _knob;
   int32_t _min = std::numeric_limits<int32_t>::min();
   int32_t _max = std::numeric_limits<int32_t>::max();
   int32_t _offset = 0;

   volatile uint16_t _pressedCount = 0;
   bool _lastPressed = false;

   int32_t _clampedPosition() const
   {
      int32_t position = _knob.get() + _offset;
      if (position < _min)
      {
         position = _min;
      }
      else if (position > _max)
      {
         position = _max;
      }
      return position;
   }

   ///
   /// <summary>
   /// Detects rising edges of the knob's pressed state so that buttonWasPressed()
   /// behaves like Button::wasPressed(), since ModulinoKnob only reports instantaneous
   /// press state.
   /// </summary>
   ///
   void _pollButton()
   {
      bool isPressedNow = _knob.isPressed();
      if (isPressedNow && !_lastPressed)
      {
         _pressedCount = _pressedCount + 1;
      }
      _lastPressed = isPressedNow;
   }

public:
   ///
   /// <summary>
   /// Constructs a ModulinoEncoder for the specified I2C address and hub port.
   /// </summary>
   /// <param name="address">I2C address of the Modulino Knob module; 0xFF for auto-discovery</param>
   /// <param name="hubPort">Optional Modulino hub port the module is connected through</param>
   ///
   ModulinoEncoder(uint8_t address = 0xFF, ModulinoHubPort* hubPort = nullptr) : _knob(address, hubPort)
   {
   }

   ///
   /// <summary>
   /// Constructs a ModulinoEncoder connected through the specified hub port.
   /// </summary>
   /// <param name="hubPort">Modulino hub port the module is connected through</param>
   /// <param name="address">I2C address of the Modulino Knob module; 0xFF for auto-discovery</param>
   ///
   ModulinoEncoder(ModulinoHubPort* hubPort, uint8_t address = 0xFF) : _knob(hubPort, address)
   {
   }

   void begin() override
   {
      // Modulino._wire must be initialized (which also calls Wire.begin()) before any
      // module can perform I2C transactions; otherwise Module::read() dereferences a
      // null HardwareI2C* and crashes.
      Modulino.begin();
      _knob.begin();
      _lastPressed = _knob.isPressed();
   }

   int32_t getPosition() const override
   {
      return _clampedPosition();
   }

   void setPosition(int32_t position) override
   {
      _offset = position - _knob.get();
   }

   void reset() override
   {
      _offset = -_knob.get();
      IEncoder::reset();
   }

   void setLimits(int32_t min, int32_t max) override
   {
      _min = min;
      _max = max;
   }

   int32_t getMin() const override
   {
      return _min;
   }

   int32_t getMax() const override
   {
      return _max;
   }

   bool isButtonPressed() const override
   {
      return _knob.isPressed();
   }

   bool buttonWasPressed() override
   {
      _pollButton();
      bool pressed = _pressedCount > 0;
      _pressedCount = 0;
      return pressed;
   }

   void resetButton() override
   {
      _pressedCount = 0;
   }
};
