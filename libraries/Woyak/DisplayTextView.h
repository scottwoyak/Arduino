#pragma once

#include <string>
#include <vector>

#include "ArduinoWithDisplay.h"
#include "Structs.h"

///
/// <summary>
/// Displays a buffer of text lines on the screen, scrollable via an externally supplied delta.
/// </summary>
/// <remarks>
/// Lines are appended with addLine()/clear() and rendered within a configurable
/// bounding rect. update() takes a scroll delta (e.g. from a rotary encoder or any other
/// input mechanism) and adjusts the scroll position accordingly, clamping to the valid
/// range. draw() redraws the visible lines unconditionally, so callers should only call
/// it when the content or scroll position has changed (e.g. after update() reports a
/// change, or after addLine()/clear()).
/// </remarks>
///
class DisplayTextView
{
private:
   ArduinoWithDisplay* _display;
   std::vector<std::string> _lines;
   Rect16 _rect;
   uint8_t _textSize;
   Color _textColor;
   Color _backgroundColor;
   int32_t _scrollOffset = 0;
   bool _needsRedraw = true;

   ///
   /// <summary>
   /// Computes the number of text lines that fit within the configured rect's height,
   /// given the current text size.
   /// </summary>
   /// <returns>Number of visible lines.</returns>
   ///
   size_t _visibleLineCount() const
   {
      int16_t lineHeight = _display->charH();
      if (lineHeight <= 0)
      {
         return 0;
      }

      return static_cast<size_t>(max(static_cast<int16_t>(0), static_cast<int16_t>(_rect.height / lineHeight)));
   }

   ///
   /// <summary>
   /// Computes the maximum valid scroll offset given the current line count and visible height.
   /// </summary>
   /// <returns>Maximum scroll offset (0 when all lines already fit on screen).</returns>
   ///
   int32_t _maxScrollOffset() const
   {
      size_t visible = _visibleLineCount();
      if (_lines.size() <= visible)
      {
         return 0;
      }

      return static_cast<int32_t>(_lines.size() - visible);
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the DisplayTextView class.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="rect">The bounding box of the text area.</param>
   /// <param name="textSize">The text size used to render each line.</param>
   /// <param name="textColor">The color used to render each line.</param>
   /// <param name="backgroundColor">The background color filled behind the text area.</param>
   ///
   DisplayTextView(ArduinoWithDisplay* display, Rect16 rect,
                    uint8_t textSize, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
      : _display(display), _rect(rect),
        _textSize(textSize), _textColor(textColor), _backgroundColor(backgroundColor)
   {
   }

   ///
   /// <summary>
   /// Removes all buffered lines and resets scrolling to the top.
   /// </summary>
   ///
   void clear()
   {
      _lines.clear();
      _scrollOffset = 0;
      _needsRedraw = true;
   }

   ///
   /// <summary>
   /// Sets the bounding box of the text area.
   /// </summary>
   /// <param name="rect">The new bounding box.</param>
   ///
   void setRect(Rect16 rect)
   {
      _rect = rect;
      _needsRedraw = true;
   }

   ///
   /// <summary>
   /// Appends a line of text to the end of the buffer.
   /// </summary>
   /// <param name="line">The line of text to append.</param>
   ///
   void addLine(const std::string& line)
   {
      _lines.push_back(line);
      _needsRedraw = true;
   }

   ///
   /// <summary>
   /// Appends a line of text to the end of the buffer.
   /// </summary>
   /// <param name="line">The line of text to append.</param>
   ///
   void addLine(const String& line)
   {
      addLine(std::string(line.c_str()));
   }

   ///
   /// <summary>
   /// Appends a line of text to the end of the buffer.
   /// </summary>
   /// <param name="line">The line of text to append.</param>
   ///
   void addLine(const char* line)
   {
      addLine(std::string(line));
   }

   ///
   /// <summary>
   /// Appends a block of text to the buffer, splitting it into separate lines on any
   /// embedded '\n' characters. A trailing '\n' does not produce an extra blank line.
   /// </summary>
   /// <param name="text">The block of text to append, using '\n' to separate lines.</param>
   ///
   void addText(const String& text)
   {
      int start = 0;
      int newlineIndex;
      while ((newlineIndex = text.indexOf('\n', start)) != -1)
      {
         addLine(text.substring(start, newlineIndex));
         start = newlineIndex + 1;
      }

      if (start < (int)text.length())
      {
         addLine(text.substring(start));
      }
   }

   ///
   /// <summary>
   /// Resets the scroll position to the top of the buffer.
   /// </summary>
   ///
   void scrollToTop()
   {
      _scrollOffset = 0;
      _needsRedraw = true;
   }

   ///
   /// <summary>
   /// Adjusts the scroll position by the given delta, clamped to the valid range for the
   /// current buffer/visible height. Callers are expected to invoke this in response to an
   /// input event (e.g. a rotary encoder callback) rather than by polling.
   /// </summary>
   /// <param name="delta">The scroll delta to apply (e.g. from a rotary encoder or other input).</param>
   ///
   void scrollBy(int32_t delta)
   {
      int32_t newOffset = constrain(_scrollOffset + delta, 0, _maxScrollOffset());
      if (newOffset != _scrollOffset)
      {
         _scrollOffset = newOffset;
         _needsRedraw = true;
      }
   }

   ///
   /// <summary>
   /// Gets whether the content or scroll position has changed since the last draw(), i.e.
   /// whether a redraw is needed.
   /// </summary>
   /// <returns>True if draw() should be called again; otherwise false.</returns>
   ///
   bool needsRedraw() const
   {
      return _needsRedraw;
   }

   ///
   /// <summary>
   /// Draws the currently visible window of lines, starting at the configured top Y
   /// coordinate. Each line is padded with spaces (using the configured background color)
   /// to fill the full width of the rect, so scrolling can simply redraw over the previous
   /// contents without first clearing/filling the display.
   /// </summary>
   ///
   void draw()
   {
      _display->setTextSize(_textSize);
      _display->setCursor(_rect.x, _rect.y);

      uint8_t charWidth = _display->charW();
      size_t columns = (charWidth > 0) ? static_cast<size_t>(_rect.width / charWidth) : 0;

      size_t visible = _visibleLineCount();
      for (size_t i = 0; i < visible; i++)
      {
         size_t lineIndex = static_cast<size_t>(_scrollOffset) + i;
         std::string line = (lineIndex < _lines.size()) ? _lines[lineIndex] : std::string();
         if (line.length() < columns)
         {
            line.append(columns - line.length(), ' ');
         }

         _display->println(line.c_str(), _textColor, _backgroundColor);
      }

      _needsRedraw = false;
   }
};
