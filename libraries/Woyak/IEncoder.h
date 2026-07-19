#pragma once

#include <Arduino.h>

// Forward declaration needed because EncoderButton holds a pointer back to the
// IEncoder that owns it, before IEncoder itself is declared below.
class IEncoder;

///
/// <summary>
/// Proxy for the integral pushbutton on an IEncoder, regardless of backend.
/// </summary>
/// <remarks>
/// Forwards button queries to the owning IEncoder so that calling code can use
/// encoder.button.wasPressed()/isPressed() the same way for every backend.
/// </remarks>
///
class EncoderButton
{
private:
   IEncoder* _encoder;

public:
   ///
   /// <summary>
   /// Constructs an EncoderButton bound to its owning encoder.
   /// </summary>
   /// <param name="encoder">The IEncoder whose button state this proxy forwards to.</param>
   ///
   EncoderButton(IEncoder* encoder) : _encoder(encoder)
   {
   }

   ///
   /// <summary>
   /// Checks if the button is currently pressed.
   /// </summary>
   ///
   bool isPressed() const;

   ///
   /// <summary>
   /// Checks if a button press occurred since the last check. The press counter is
   /// cleared after this call.
   /// </summary>
   ///
   bool wasPressed();

   ///
   /// <summary>
   /// Manually resets the press counter to zero.
   /// </summary>
   ///
   void reset();
};

///
/// <summary>
/// Interface for rotary encoder hardware with an integral pushbutton.
/// </summary>
/// <remarks>
/// Implemented directly by concrete encoder types (e.g. QuadratureEncoder for GPIO
/// wiring, ModulinoEncoder for an I2C Modulino Knob module) so that calling code can
/// use the same API regardless of which hardware backend is in use.
/// </remarks>
///
class IEncoder
{
private:
   int32_t _lastPosition = 0;

public:
   ///
   /// <summary>
   /// Integral button on the rotary encoder shaft.
   /// </summary>
   ///
   EncoderButton button;

   ///
   /// <summary>
   /// Constructs an IEncoder, binding its integral button proxy to itself.
   /// </summary>
   ///
   IEncoder() : button(this)
   {
   }

   virtual ~IEncoder() = default;

   // button holds a raw pointer back to its owning IEncoder, so copying/assigning
   // would leave the copy's button pointing at the original instance instead of
   // itself.
   IEncoder(const IEncoder&) = delete;
   IEncoder& operator=(const IEncoder&) = delete;

   ///
   /// <summary>
   /// Initializes the underlying hardware. Call once during setup().
   /// </summary>
   ///
   virtual void begin() = 0;

   ///
   /// <summary>
   /// Gets the current rotation position.
   /// </summary>
   ///
   virtual int32_t getPosition() const = 0;

   ///
   /// <summary>
   /// Sets the rotation position.
   /// </summary>
   ///
   virtual void setPosition(int32_t position) = 0;

   ///
   /// <summary>
   /// Gets the change in position since the last call to delta(), then resets the
   /// tracked baseline to the current position.
   /// </summary>
   /// <returns>Signed change in position since the last call to delta().</returns>
   ///
   int32_t delta()
   {
      int32_t position = getPosition();
      int32_t change = position - _lastPosition;
      _lastPosition = position;
      return change;
   }

   ///
   /// <summary>
   /// Resets the current position and the delta() baseline to zero, and clears any
   /// implementation-specific in-progress transition state.
   /// </summary>
   ///
   virtual void reset()
   {
      _lastPosition = 0;
   }

   ///
   /// <summary>
   /// Sets the min/max position limits.
   /// </summary>
   /// <remarks>
   /// Position will be clamped to this range during rotation.
   /// </remarks>
   ///
   virtual void setLimits(int32_t min, int32_t max) = 0;

   ///
   /// <summary>
   /// Gets the minimum position limit.
   /// </summary>
   ///
   virtual int32_t getMin() const = 0;

   ///
   /// <summary>
   /// Gets the maximum position limit.
   /// </summary>
   ///
   virtual int32_t getMax() const = 0;

   ///
   /// <summary>
   /// Checks if the integral button is currently pressed.
   /// </summary>
   ///
   virtual bool isButtonPressed() const = 0;

   ///
   /// <summary>
   /// Checks if a button press occurred since the last check.
   /// </summary>
   ///
   virtual bool buttonWasPressed() = 0;

   ///
   /// <summary>
   /// Manually resets the integral button's press counter.
   /// </summary>
   ///
   virtual void resetButton() = 0;
};

inline bool EncoderButton::isPressed() const
{
   return _encoder->isButtonPressed();
}

inline bool EncoderButton::wasPressed()
{
   return _encoder->buttonWasPressed();
}

inline void EncoderButton::reset()
{
   _encoder->resetButton();
}
