#pragma once

#include <string>
#include <vector>

// Included at the end of ArduinoWithDisplay.h, which must be fully defined first.
#include "ColorX.h"
#include "Util.h"

///
/// <summary>
/// Shows initialization progress on the display. Each step is a label on the left (in the
/// LABEL color) followed, once the step completes, by a result such as "OK" at the right
/// edge (in the VALUE color). The text fills the display below the header; when a new line
/// would extend past the bottom, older lines scroll up so the newest line is always fully
/// visible.
/// </summary>
/// <remarks>
/// Call begin() after drawing the header (e.g. printInitHeader()), since the text area
/// starts at the current cursor Y position and extends to the bottom of the display.
/// Rows are drawn in place without erasing, so updating a row does not flicker.
/// </remarks>
///
class InitializingDisplay
{
private:
   struct Line
   {
      std::string label;
      Color labelColor;
      std::string value;
      Color valueColor;
   };

   ArduinoWithDisplay* _display;
   uint8_t _textSize;
   int16_t _top = 0;
   std::vector<Line> _lines;

   ///
   /// <summary>
   /// Gets the number of text rows that fit below the header.
   /// </summary>
   /// <returns>Number of fully visible rows.</returns>
   ///
   size_t _maxRows()
   {
      return (_display->height() - _top) / _display->charH(_textSize);
   }

   ///
   /// <summary>
   /// Gets the number of characters that fit across the display.
   /// </summary>
   /// <returns>Number of characters per row.</returns>
   ///
   size_t _maxChars()
   {
      return _display->width() / _display->charW(_textSize);
   }

   ///
   /// <summary>
   /// Draws a single row in place, without erasing it first, so there is no flicker. The
   /// label, background-colored padding and right-aligned value together cover the whole
   /// row, overwriting whatever was there before.
   /// </summary>
   /// <param name="index">Index of the line to draw.</param>
   ///
   void _drawRow(size_t index)
   {
      const Line& line = _lines[index];
      size_t padding = _maxChars() - line.label.length() - line.value.length();

      _display->setTextSize(_textSize);
      _display->setCursor(0, _top + (int16_t)index * _display->charH(_textSize));
      _display->print(line.label, line.labelColor);
      _display->print(std::string(padding, ' '));
      _display->print(line.value, line.valueColor);
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the InitializingDisplay class.
   /// </summary>
   /// <param name="display">The display to draw on.</param>
   /// <param name="textSize">Text size used for all lines.</param>
   ///
   InitializingDisplay(
      ArduinoWithDisplay* display,
      uint8_t textSize = 2)
   {
      ASSERT(display != nullptr);

      _display = display;
      _textSize = textSize;
   }

   ///
   /// <summary>
   /// Starts the text area at the display's current cursor Y position. Call after the
   /// header has been drawn.
   /// </summary>
   ///
   void begin()
   {
      _top = _display->getCursorY();
      _lines.clear();
   }

   ///
   /// <summary>
   /// Adds a line containing only the label, scrolling older lines up if the display is
   /// full. Call setValue() once the step is done to show its result.
   /// </summary>
   /// <param name="label">Label text, e.g. "WiFi...".</param>
   ///
   void printLabel(const char* label, Color labelColor = Color::LABEL)
   {
      _lines.push_back({ label, labelColor, std::string(), Color::VALUE });
      if (_lines.size() > _maxRows())
      {
         _lines.erase(_lines.begin());
         for (size_t i = 0; i < _lines.size(); i++)
         {
            _drawRow(i);
         }
      }
      else
      {
         _drawRow(_lines.size() - 1);
      }
   }

   ///
   /// <summary>
   /// Sets the right-aligned result of the most recently added line. The label is
   /// truncated if it would overlap the value.
   /// </summary>
   /// <param name="value">Result text, e.g. "OK".</param>
   /// <param name="valueColor">Result color.</param>
   ///
   void setValue(const char* value, Color valueColor = Color::VALUE)
   {
      ASSERT(!_lines.empty());

      Line& line = _lines.back();
      line.value = value;
      line.valueColor = valueColor;

      size_t maxLabelChars = _maxChars() > line.value.length() ? _maxChars() - line.value.length() : 0;
      if (line.label.length() > maxLabelChars)
      {
         line.label.resize(maxLabelChars);
      }

      _drawRow(_lines.size() - 1);
   }

   ///
   /// <summary>
   /// Removes all lines and erases the text area.
   /// </summary>
   ///
   void clear()
   {
      _lines.clear();
      _display->fillRect(0, _top, _display->width(), _display->height() - _top, Color::BLACK);
   }
};
