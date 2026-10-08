#pragma once

#include "ArduinoWithDisplay.h"
#include "ColorX.h"

///
/// <summary>
/// A horizontal touch slider that adjusts a float value within a range. The slider draws
/// itself (label, value, track and knob) into an off-screen sprite to avoid flicker.
/// </summary>
///
class Slider
{
private:
   ArduinoWithDisplay* _arduino;
   const char* _label;
   uint8_t _decimals;
   const char* _suffix;
   float _minValue;
   float _maxValue;
   float* _value;
   int16_t _x;
   int16_t _y;
   int16_t _width;
   int16_t _height;
   int16_t _margin;
   uint8_t _textSize;
   bool _dragging = false;
   LGFX_Sprite _sprite;
   bool _spriteCreated = false;

   ///
   /// <summary>
   /// Gets the width of the track.
   /// </summary>
   /// <returns>Track width in pixels</returns>
   ///
   int16_t _trackWidth() const
   {
      return _width - 2 * _margin;
   }

   ///
   /// <summary>
   /// Converts the current value to a knob x position, relative to the slider's left edge.
   /// </summary>
   /// <returns>Knob center x coordinate</returns>
   ///
   int16_t _knobX() const
   {
      return _margin + (int16_t)((*_value - _minValue) / (_maxValue - _minValue) * _trackWidth());
   }

public:
   /// Track height in pixels.
   int16_t trackHeight = 8;

   /// Knob radius in pixels.
   int16_t knobRadius = 10;

   /// Color of the unfilled part of the track.
   uint16_t trackColor = 0x7BEF;

   /// Color of the filled part of the track.
   uint16_t fillColor = 0x041F;

   ///
   /// <summary>
   /// Initializes a new instance of the Slider class.
   /// </summary>
   /// <param name="arduino">Board whose display the slider is drawn on</param>
   /// <param name="label">Label drawn above the track</param>
   /// <param name="decimals">Number of decimal places to display</param>
   /// <param name="suffix">Text appended to the displayed value</param>
   /// <param name="minValue">Minimum value</param>
   /// <param name="maxValue">Maximum value</param>
   /// <param name="value">Pointer to the value the slider controls</param>
   /// <param name="x">Left x coordinate of the slider</param>
   /// <param name="y">Top y coordinate of the slider</param>
   /// <param name="width">Width of the slider in pixels, including margins</param>
   /// <param name="height">Height of the slider in pixels</param>
   /// <param name="margin">Left and right margin around the label and track</param>
   /// <param name="textSize">Text size of the label</param>
   ///
   Slider(
      ArduinoWithDisplay* arduino,
      const char* label,
      uint8_t decimals,
      const char* suffix,
      float minValue,
      float maxValue,
      float* value,
      int16_t x,
      int16_t y,
      int16_t width,
      int16_t height = 56,
      int16_t margin = 20,
      uint8_t textSize = 2) :
      _arduino(arduino),
      _label(label),
      _decimals(decimals),
      _suffix(suffix),
      _minValue(minValue),
      _maxValue(maxValue),
      _value(value),
      _x(x),
      _y(y),
      _width(width),
      _height(height),
      _margin(margin),
      _textSize(textSize),
      _sprite(&arduino->display)
   {
   }

   ///
   /// <summary>
   /// Sets the slider width. Call before the first draw if the width isn't known at construction time.
   /// </summary>
   /// <param name="width">Width of the slider in pixels, including margins</param>
   ///
   void setWidth(int16_t width)
   {
      _width = width;
   }

   ///
   /// <summary>
   /// Draws the slider's label, value, track and knob.
   /// </summary>
   ///
   void draw()
   {
      if (!_spriteCreated)
      {
         _arduino->createSprite(_sprite, _width, _height, _textSize);
         _spriteCreated = true;
      }

      _sprite.fillScreen((uint16_t)Color::BLACK);

      const String text = String(_label) + ": " + String(*_value, (unsigned int)_decimals) + _suffix;
      _sprite.setTextColor((uint16_t)Color::WHITE, (uint16_t)Color::BLACK);
      _sprite.setCursor(_margin, 0);
      _sprite.print(text);

      const int16_t trackY = _arduino->charH(_textSize) + 20;
      const int16_t knobX = _knobX();
      _sprite.fillRect(_margin, trackY - trackHeight / 2, _trackWidth(), trackHeight, trackColor);
      _sprite.fillRect(_margin, trackY - trackHeight / 2, knobX - _margin, trackHeight, fillColor);
      _sprite.fillCircle(knobX, trackY, knobRadius, (uint16_t)Color::WHITE);
      _sprite.pushSprite(_x, _y);
   }

   ///
   /// <summary>
   /// Updates the slider from the current touch state.
   /// </summary>
   /// <param name="touched">True if the display is currently touched</param>
   /// <param name="x">Touch x coordinate</param>
   /// <param name="y">Touch y coordinate</param>
   /// <returns>True if the slider was just released after being dragged</returns>
   ///
   bool update(bool touched, int16_t x, int16_t y)
   {
      if (!touched)
      {
         const bool released = _dragging;
         _dragging = false;
         return released;
      }

      if (!_dragging)
      {
         _dragging = x >= _x && x < _x + _width && y >= _y && y < _y + _height;
      }

      if (_dragging)
      {
         const float fraction = constrain((float)(x - _x - _margin) / _trackWidth(), 0.0f, 1.0f);
         *_value = _minValue + fraction * (_maxValue - _minValue);
         draw();
      }

      return false;
   }
};
