//
// Sound
//
// Plays the notification sound on the Viewer board's speaker and provides touch sliders
// for tweaking the volume:
// - Volume: software playback gain applied to the sound samples.
// - Codec: the ES8311 codec's DAC volume register (0 - 197).
//
// The selector row chooses the sound and plays it each time a sound name is pressed,
// even if it is already selected. Releasing either slider also plays the selected sound.
//
// Hardware: Hosyond 4" ESP32-S3 Viewer board with a speaker connected to the horn connector.
//

#define ARDUINO_HOSYOND_ESP32_S3_VIEWER

// Only compile in the font sizes this sketch uses, to save flash.
#define TEXT_SIZES_CUSTOM
#define TEXT_SIZE_2
#define TEXT_SIZE_3
#define TEXT_SIZE_4

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Viewer)."
#include "WrongBoard.h"
#endif

#include "SerialX.h"

Arduino arduino;

constexpr auto TITLE = "Sound";

constexpr uint8_t TITLE_TEXT_SIZE = 4;
constexpr uint8_t LABEL_TEXT_SIZE = 2;

constexpr float NOTE_C5 = 523.25f;
constexpr uint16_t TEST_TONE_MILLIS = 300;

constexpr int16_t SLIDER_MARGIN = 20;
constexpr int16_t SLIDER_TOP = 50;
constexpr int16_t SLIDER_HEIGHT = 56;
constexpr int16_t TRACK_HEIGHT = 8;
constexpr int16_t KNOB_RADIUS = 14;

constexpr int16_t SELECTOR_TOP = SLIDER_TOP + 2 * SLIDER_HEIGHT + 4;
constexpr int16_t SELECTOR_HEIGHT = 30;
constexpr int16_t SELECTOR_GAP = 4;
constexpr uint8_t SELECTOR_COLUMNS = 3;
constexpr uint8_t SELECTOR_TEXT_SIZE = 2;

constexpr uint16_t TRACK_COLOR = 0x7BEF;
constexpr uint16_t FILL_COLOR = 0x041F;

///
/// <summary>
/// A horizontal touch slider that adjusts a float value within a range.
/// </summary>
///
class Slider
{
private:
   const char* _label;
   uint8_t _decimals;
   const char* _suffix;
   float _minValue;
   float _maxValue;
   float* _value;
   int16_t _y;
   bool _dragging = false;
   LGFX_Sprite _sprite;
   bool _spriteCreated = false;

   ///
   /// <summary>
   /// Converts the current value to a knob x position.
   /// </summary>
   /// <returns>Knob center x coordinate</returns>
   ///
   int16_t _knobX() const
   {
      const int16_t trackWidth = arduino.width() - 2 * SLIDER_MARGIN;
      return SLIDER_MARGIN + (int16_t)((*_value - _minValue) / (_maxValue - _minValue) * trackWidth);
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the Slider class.
   /// </summary>
   /// <param name="label">Label drawn above the slider</param>
   /// <param name="decimals">Number of decimal places to display</param>
   /// <param name="suffix">Text appended to the displayed value</param>
   /// <param name="minValue">Minimum value</param>
   /// <param name="maxValue">Maximum value</param>
   /// <param name="value">Pointer to the value the slider controls</param>
   /// <param name="y">Top y coordinate of the slider</param>
   ///
   Slider(const char* label, uint8_t decimals, const char* suffix, float minValue, float maxValue, float* value, int16_t y) :
      _label(label),
      _decimals(decimals),
      _suffix(suffix),
      _minValue(minValue),
      _maxValue(maxValue),
      _value(value),
      _y(y),
      _sprite(&arduino.display)
   {
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
         arduino.createSprite(_sprite, arduino.width(), SLIDER_HEIGHT, LABEL_TEXT_SIZE);
         _spriteCreated = true;
      }

      _sprite.fillScreen((uint16_t)Color::BLACK);

      const String text = String(_label) + ": " + String(*_value, (unsigned int)_decimals) + _suffix;
      _sprite.setTextColor((uint16_t)Color::WHITE, (uint16_t)Color::BLACK);
      _sprite.setCursor(SLIDER_MARGIN, 0);
      _sprite.print(text);

      const int16_t trackY = arduino.charH(LABEL_TEXT_SIZE) + 20;
      const int16_t trackWidth = arduino.width() - 2 * SLIDER_MARGIN;
      const int16_t knobX = _knobX();
      _sprite.fillRect(SLIDER_MARGIN, trackY - TRACK_HEIGHT / 2, trackWidth, TRACK_HEIGHT, TRACK_COLOR);
      _sprite.fillRect(SLIDER_MARGIN, trackY - TRACK_HEIGHT / 2, knobX - SLIDER_MARGIN, TRACK_HEIGHT, FILL_COLOR);
      _sprite.fillCircle(knobX, trackY, KNOB_RADIUS, (uint16_t)Color::WHITE);
      _sprite.pushSprite(0, _y);
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
         _dragging = y >= _y && y < _y + SLIDER_HEIGHT;
      }

      if (_dragging)
      {
         const int16_t trackWidth = arduino.width() - 2 * SLIDER_MARGIN;
         const float fraction = constrain((float)(x - SLIDER_MARGIN) / trackWidth, 0.0f, 1.0f);
         *_value = _minValue + fraction * (_maxValue - _minValue);
         draw();
      }

      return false;
   }
};

float codecVolume = 197.0f;
bool selectorPressed = false;

///
/// <summary>
/// Gets the bounds of a sound button in the selector grid.
/// </summary>
/// <param name="index">Sound index</param>
/// <param name="x">Receives the left x coordinate</param>
/// <param name="y">Receives the top y coordinate</param>
/// <param name="width">Receives the width</param>
///
void getSelectorCell(uint8_t index, int16_t* x, int16_t* y, int16_t* width)
{
   const int16_t cellWidth = (arduino.width() - 2 * SLIDER_MARGIN) / SELECTOR_COLUMNS;
   *x = SLIDER_MARGIN + (index % SELECTOR_COLUMNS) * cellWidth;
   *y = SELECTOR_TOP + (index / SELECTOR_COLUMNS) * SELECTOR_HEIGHT;
   *width = cellWidth - SELECTOR_GAP;
}

///
/// <summary>
/// Draws the grid of sound selection buttons, highlighting the selected sound.
/// </summary>
///
void drawSelector()
{
   arduino.setTextSize(SELECTOR_TEXT_SIZE);
   for (uint8_t i = 0; i < Sound::NUM_SOUNDS; i++)
   {
      int16_t x;
      int16_t y;
      int16_t width;
      getSelectorCell(i, &x, &y, &width);

      const uint16_t fillColor = (uint16_t)(i == arduino.sound.soundIndex ? Color::BLUE : Color::DARKGRAY);
      const char* name = Sound::soundName(i);
      arduino.display.fillRect(x, y, width, SELECTOR_HEIGHT - SELECTOR_GAP, fillColor);
      arduino.display.setTextColor((uint16_t)Color::WHITE, fillColor);
      arduino.display.setCursor(
         x + (width - arduino.display.textWidth(name)) / 2,
         y + (SELECTOR_HEIGHT - SELECTOR_GAP - arduino.charH(SELECTOR_TEXT_SIZE)) / 2);
      arduino.display.print(name);
   }
}

Slider volumeSlider("Volume", 2, "", 0.0f, 4.0f, &arduino.sound.volume, SLIDER_TOP);
Slider codecSlider("Codec", 0, "", 0.0f, 197.0f, &codecVolume, SLIDER_TOP + SLIDER_HEIGHT);

void setup()
{
   SerialX::begin();
   arduino.begin();

   arduino.sound.setDacVolume((uint8_t)codecVolume);

   arduino.clearDisplay();
   arduino.setCursor(SLIDER_MARGIN, 0);
   arduino.setTextSize(TITLE_TEXT_SIZE);
   arduino.println(TITLE, Color::HEADING);

   volumeSlider.draw();
   codecSlider.draw();
   drawSelector();
}

void loop()
{
   lgfx::touch_point_t touchPoint;
   const bool touched = arduino.display.getTouch(&touchPoint) > 0;

   const int16_t numRows = (Sound::NUM_SOUNDS + SELECTOR_COLUMNS - 1) / SELECTOR_COLUMNS;
   const bool onSelector = touched &&
      touchPoint.y >= SELECTOR_TOP &&
      touchPoint.y < SELECTOR_TOP + numRows * SELECTOR_HEIGHT &&
      touchPoint.x >= SLIDER_MARGIN;
   if (onSelector && !selectorPressed)
   {
      const int16_t cellWidth = (arduino.width() - 2 * SLIDER_MARGIN) / SELECTOR_COLUMNS;
      const int16_t column = (touchPoint.x - SLIDER_MARGIN) / cellWidth;
      const int16_t row = (touchPoint.y - SELECTOR_TOP) / SELECTOR_HEIGHT;
      const int16_t index = row * SELECTOR_COLUMNS + column;
      if (column < SELECTOR_COLUMNS && index < Sound::NUM_SOUNDS)
      {
         if (index != arduino.sound.soundIndex)
         {
            arduino.sound.soundIndex = index;
            drawSelector();
         }
         arduino.sound.playNotificationAsync();
      }
   }
   selectorPressed = onSelector;

   if (volumeSlider.update(touched, touchPoint.x, touchPoint.y))
   {
      arduino.sound.playNotificationAsync();
   }

   if (codecSlider.update(touched, touchPoint.x, touchPoint.y))
   {
      arduino.sound.setDacVolume((uint8_t)codecVolume);
      arduino.sound.playNotificationAsync();
   }
}
