#pragma once

#include "LGX_HosyondESP32S3.h"
#include "ArduinoWithDisplay.h"
#include "Button.h"
#include "MultiStatus.h"
#include <Preferences.h>

///
/// <summary>
/// ViewerBoardS3 board wrapper. Hosyond ESP32-S3 dev board with a builtin 4" ST7796S
/// TFT display and FT6336U capacitive touch controller, using the BOOT button (GPIO0)
/// as buttonA.
/// </summary>
///
class ViewerBoardS3 : public ArduinoWithDisplay, public IStatus
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
   /// Status indicator; this board has no physical NeoPixel, so no underlying
   /// indicators are wired in for now.
   /// </summary>
   ///
   MultiStatus status;

   ///
   /// <summary>
   /// Initializes a new instance of the ViewerBoardS3 class.
   /// </summary>
   ///
   ViewerBoardS3() : ArduinoWithDisplay(), buttonA(0)
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

       ///
       /// <summary>
       /// Updates the status indicator to reflect the specified status.
       /// </summary>
       /// <param name="status">The status value to display.</param>
       ///
       void setStatus(Status status) override
       {
          this->status.setStatus(status);
       }
   };
