#pragma once

#include "LGX_HosyondESP32S3.h"
#include "ArduinoWithDisplay.h"
#include "Button.h"
#include "MultiStatus.h"
#include <Preferences.h>
#include "VirtualNeoPixel.h"

///
/// <summary>
/// ViewerBoardS3 board wrapper. Hosyond ESP32-S3 dev board with a builtin 4" ST7796S
/// TFT display and FT6336U capacitive touch controller, using the BOOT button (GPIO0)
/// as buttonA.
/// </summary>
///
class ViewerBoardS3 : public ArduinoWithDisplay
{
private:
   ///
   /// <summary>
   /// GPIO pin wired to the FT6336U touch controller's RST line. Must be held high for
   /// the controller to operate normally.
   /// </summary>
   ///
   static constexpr uint8_t TOUCH_RESET_PIN = 18;

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
   /// Initializes a new instance of the ViewerBoardS3 class.
   /// </summary>
   ///
   ViewerBoardS3() : ArduinoWithDisplay(), buttonA(0), neoPixel(&display), _virtualNeoPixelStatus(&neoPixel),
      status(&_virtualNeoPixelStatus)
   {
   }

   ///
   /// <summary>
   /// Initializes the display, the FT6336U touch controller's RST line, and the BOOT
   /// button.
   /// </summary>
   ///
   void begin() override
   {
      pinMode(TOUCH_RESET_PIN, OUTPUT);
      digitalWrite(TOUCH_RESET_PIN, HIGH);

      ArduinoWithDisplay::begin();

      buttonA.begin();
      status.begin();
   }

   ///
   /// <summary>
   /// Uses one text size larger than the base default for initialization headers,
   /// since the ViewerBoardS3's larger 4" display has room for bigger text.
   /// </summary>
   /// <returns>Text size to use for headers.</returns>
   ///
   uint8_t headerTextSize() override
   {
      return ArduinoWithDisplay::headerTextSize() + 1;
   }
};
