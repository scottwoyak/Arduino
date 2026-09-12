#pragma once

#include "ColorX.h"

///
/// <summary>
/// Common text output contract shared by display-capable boards (ArduinoWithDisplay) and
/// plain serial-only boards (via ArduinoBase's default implementation). Lets shared
/// startup/init logic (see ArduinoBase.h)
/// print consistently formatted status text regardless of whether a display is present.
/// </summary>
///
class IPrinter
{
public:
   virtual ~IPrinter() = default;

   ///
   /// <summary>
   /// Prints text without a trailing newline.
   /// </summary>
   /// <param name="str">The text to print.</param>
   /// <param name="textColor">The text color (ignored on serial-only implementations).</param>
   /// <param name="backgroundColor">The background color (ignored on serial-only implementations).</param>
   ///
   virtual void print(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK) = 0;

   ///
   /// <summary>
   /// Prints text followed by a newline.
   /// </summary>
   /// <param name="str">The text to print.</param>
   /// <param name="textColor">The text color (ignored on serial-only implementations).</param>
   /// <param name="backgroundColor">The background color (ignored on serial-only implementations).</param>
   ///
   virtual void println(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK) = 0;

   ///
   /// <summary>
   /// Prints text right-appended to the current line (e.g. "OK" after a "Label..." prefix
   /// printed via print()), followed by a newline.
   /// </summary>
   /// <param name="str">The text to print.</param>
   /// <param name="textColor">The text color (ignored on serial-only implementations).</param>
   /// <param name="backgroundColor">The background color (ignored on serial-only implementations).</param>
   ///
   virtual void printlnR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK) = 0;

   ///
   /// <summary>
   /// Prints the sketch's initialization header. Serial-only implementations just print the
   /// text; display-capable implementations (e.g. ArduinoWithDisplay) additionally clear the
   /// display, set the header text size, and move the cursor down afterward.
   /// </summary>
   /// <param name="str">The header text to print (e.g. "Initializing").</param>
   /// <param name="textColor">The text color.</param>
   ///
   virtual void printInitHeader(const char* str, Color textColor = Color::HEADING)
   {
      println(str, textColor);
   }
};
