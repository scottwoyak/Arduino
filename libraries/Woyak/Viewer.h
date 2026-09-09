#pragma once

#include "LGX_HosyondESP32-32E.h"
#include "ArduinoWithDisplay.h"
#include "Button.h"
#include "MultiStatus.h"
#include <Preferences.h>
#include "VirtualNeoPixel.h"

///
/// <summary>
/// Viewer board wrapper. Hosyond ESP32-32E dev board with a builtin 4" ST7796 TFT
/// display, using the BOOT button (GPIO0) as buttonA.
/// </summary>
///
class Viewer : public ArduinoWithDisplay
{
public:
   ///
   /// <summary>
   /// BOOT pushbutton, wired to GPIO0.
   /// </summary>
   ///
   Button buttonA;

   ///
   /// <summary>
   /// Non-volatile storage for persisting settings across power cycles.
   /// </summary>
   ///
   Preferences preferences;

   ///
   /// <summary>
   /// Virtual NeoPixel, drawn as a small circle in the upper-right corner of the
   /// display, since this board has no physical NeoPixel.
   /// </summary>
   ///
   VirtualNeoPixelLED neoPixel;

private:
   VirtualNeoPixelStatus _virtualNeoPixelStatus;

public:
   ///
   /// <summary>
   /// Status indicator that drives the virtual NeoPixel; Serial output isn't needed
   /// since this board's display already shows status visually.
   /// </summary>
   ///
   MultiStatus status;

   ///
   /// <summary>
   /// Initializes a new instance of the Viewer class.
   /// </summary>
   ///
   Viewer() : ArduinoWithDisplay(), buttonA(0), neoPixel(&display), _virtualNeoPixelStatus(&neoPixel),
      status(&_virtualNeoPixelStatus)
   {
   }

   ///
   /// <summary>
   /// Initializes the display and the BOOT button.
   /// </summary>
   ///
   void begin() override
   {
      ArduinoWithDisplay::begin();

      buttonA.begin();
      status.begin();
   }

   ///
   /// <summary>
   /// Uses one text size larger than the base default for initialization headers,
   /// since the Viewer's larger 4" display has room for bigger text.
   /// </summary>
   /// <returns>Text size to use for headers.</returns>
   ///
   uint8_t headerTextSize() override
   {
      return ArduinoWithDisplay::headerTextSize() + 1;
   }

   ///
   /// <summary>
   /// Hides the virtual NeoPixel so it stops overwriting display content drawn in its
   /// corner. Serial status output is unaffected.
   /// </summary>
   ///
   void hideStatus()
   {
      _virtualNeoPixelStatus.hide();
   }
};
