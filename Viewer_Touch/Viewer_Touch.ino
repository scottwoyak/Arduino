//
// Viewer Touch
//
// Demonstrates capacitive touch input on the Hosyond ESP32-S3 4" display (Viewer
// board). Draws four on-screen buttons (two on top, two below) that change color
// while touched and revert when released (or when the touch moves off the button),
// so both horizontal and vertical touch alignment can be verified.
//

#define ARDUINO_HOSYOND_ESP32_S3_VIEWER

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Viewer)."
#endif

#include "SerialX.h"

Arduino arduino;

constexpr auto TITLE = "Touch Demo";

constexpr uint8_t TITLE_TEXT_SIZE = 4;
constexpr uint8_t BUTTON_TEXT_SIZE = 3;

constexpr uint16_t BUTTON_WIDTH = 180;
constexpr uint16_t BUTTON_HEIGHT = 100;
constexpr uint16_t BUTTON_COL_GAP = 30;
constexpr uint16_t BUTTON_ROW_GAP = 30;

Color NORMAL_COLOR = Color::BLUE;
Color PRESSED_COLOR = Color::LIGHTBLUE;

///
/// <summary>
/// An on-screen rectangular button that redraws itself with a different fill color
/// while touched.
/// </summary>
///
class TouchButton
{
private:
   Rect16 _rect;
   const char* _label;
   bool _pressed = false;

   ///
   /// <summary>
   /// Fills the button rectangle and draws its centered label.
   /// </summary>
   /// <param name="fillColor">Color to fill the button background with</param>
   ///
   void _draw(Color fillColor)
   {
      arduino.display.fillRect(_rect.x, _rect.y, _rect.width, _rect.height, (uint16_t)fillColor);
      arduino.display.setTextColor((uint16_t)Color::WHITE, (uint16_t)fillColor);
      arduino.setTextSize(BUTTON_TEXT_SIZE);

      int16_t textX = _rect.x + (_rect.width - arduino.display.textWidth(_label)) / 2;
      int16_t textY = _rect.y + (_rect.height - arduino.charH(BUTTON_TEXT_SIZE)) / 2;
      arduino.display.setCursor(textX, textY);
      arduino.display.print(_label);
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the TouchButton class.
   /// </summary>
   /// <param name="rect">Screen position and size of the button</param>
   /// <param name="label">Text label drawn centered on the button</param>
   ///
   TouchButton(Rect16 rect, const char* label) : _rect(rect), _label(label)
   {
   }

   ///
   /// <summary>
   /// Draws the button in its normal (unpressed) color.
   /// </summary>
   ///
   void draw()
   {
      _draw(NORMAL_COLOR);
   }

   ///
   /// <summary>
   /// Returns true if the given touch point falls within the button's bounds.
   /// </summary>
   /// <param name="x">Touch X coordinate</param>
   /// <param name="y">Touch Y coordinate</param>
   /// <returns>True if the point is inside the button</returns>
   ///
   bool contains(int16_t x, int16_t y) const
   {
      return x >= _rect.left() && x < _rect.right() && y >= _rect.top() && y < _rect.bottom();
   }

   ///
   /// <summary>
   /// Updates the button's pressed state, redrawing it only when the state changes.
   /// </summary>
   /// <param name="touched">True if a touch point currently falls within the button</param>
   ///
   void update(bool touched)
   {
      if (touched == _pressed)
      {
         return;
      }

      _pressed = touched;
      _draw(_pressed ? PRESSED_COLOR : NORMAL_COLOR);
   }
};

TouchButton button1(
   Rect16(0, 0, BUTTON_WIDTH, BUTTON_HEIGHT),
   "Button 1");

TouchButton button2(
   Rect16(0, 0, BUTTON_WIDTH, BUTTON_HEIGHT),
   "Button 2");

TouchButton button3(
   Rect16(0, 0, BUTTON_WIDTH, BUTTON_HEIGHT),
   "Button 3");

TouchButton button4(
   Rect16(0, 0, BUTTON_WIDTH, BUTTON_HEIGHT),
   "Button 4");

///
/// <summary>
/// Draws the title header at the top of the display.
/// </summary>
///
void displayHeader()
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(TITLE_TEXT_SIZE);
   arduino.println(TITLE, Color::HEADING);
}

void setup()
{
   SerialX::begin();
   arduino.begin();

   int16_t totalWidth = BUTTON_WIDTH * 2 + BUTTON_COL_GAP;
   int16_t totalHeight = BUTTON_HEIGHT * 2 + BUTTON_ROW_GAP;
   int16_t left = (arduino.width() - totalWidth) / 2;
   int16_t top = (arduino.height() - totalHeight) / 2;

   button1 = TouchButton(Rect16(left, top, BUTTON_WIDTH, BUTTON_HEIGHT), "Button 1");
   button2 = TouchButton(Rect16(left + BUTTON_WIDTH + BUTTON_COL_GAP, top, BUTTON_WIDTH, BUTTON_HEIGHT), "Button 2");
   button3 = TouchButton(Rect16(left, top + BUTTON_HEIGHT + BUTTON_ROW_GAP, BUTTON_WIDTH, BUTTON_HEIGHT), "Button 3");
   button4 = TouchButton(Rect16(left + BUTTON_WIDTH + BUTTON_COL_GAP, top + BUTTON_HEIGHT + BUTTON_ROW_GAP, BUTTON_WIDTH, BUTTON_HEIGHT), "Button 4");

   arduino.clearDisplay();
   displayHeader();
   button1.draw();
   button2.draw();
   button3.draw();
   button4.draw();
}

void loop()
{
   lgfx::touch_point_t touchPoint;
   bool touched = arduino.display.getTouch(&touchPoint) > 0;

   button1.update(touched && button1.contains(touchPoint.x, touchPoint.y));
   button2.update(touched && button2.contains(touchPoint.x, touchPoint.y));
   button3.update(touched && button3.contains(touchPoint.x, touchPoint.y));
   button4.update(touched && button4.contains(touchPoint.x, touchPoint.y));
}
