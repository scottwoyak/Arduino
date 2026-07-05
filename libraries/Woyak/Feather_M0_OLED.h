#pragma once

#include "ArduinoWithDisplay.h"
#include "Button.h"
#include <Adafruit_SH110X.h>
#include <FlashStorage.h>

/// <summary>
/// SH1107 wrapper that exposes Adafruit_GFX display metrics.
/// </summary>
class SW_SH1107 : public Adafruit_SH1107, public Adafruit_GFX_wInfo
{
public:
   /// <summary>
   /// Initializes an SH1107 display instance.
   /// </summary>
   /// <param name="w">Display width in pixels.</param>
   /// <param name="h">Display height in pixels.</param>
   /// <param name="twi">I2C bus instance.</param>
   SW_SH1107(uint16_t w, uint16_t h, TwoWire* twi = &Wire) : Adafruit_SH1107(w, h, twi)
   {
   }

   /// <summary>
   /// Gets the current character width in pixels.
   /// </summary>
   /// <returns>Character width in pixels.</returns>
   uint8_t charW()
   {
      return 6 * textsize_x;
   }

   /// <summary>
   /// Gets the current character height in pixels.
   /// </summary>
   /// <returns>Character height in pixels.</returns>
   uint8_t charH()
   {
      return 8 * textsize_y;
   }
};

/// <summary>
/// Minimal flash-backed data for preference emulation.
/// </summary>
struct FlashData
{
   boolean valid;
   char json[100];
};

FlashStorage(flash, FlashData);

/// <summary>
/// Lightweight Preferences-compatible wrapper using FlashStorage.
/// </summary>
class PreferencesFlash
{
private:
   FlashData data;

public:
   /// <summary>
   /// Loads persisted data from flash.
   /// </summary>
   /// <returns>Always true.</returns>
   bool begin(const char* name, bool readOnly = false)
   {
      (void)name;
      (void)readOnly;
      data = flash.read();
      return true;
   }

   /// <summary>
   /// Persists current data to flash.
   /// </summary>
   void end()
   {
      flash.write(data);
   }

   /// <summary>
   /// Checks whether a value has been stored.
   /// </summary>
   bool isKey(const char* key)
   {
      (void)key;
      return data.valid;
   }

   /// <summary>
   /// Stores a string value.
   /// </summary>
   size_t putString(const char* key, String value)
   {
      (void)key;
      value.toCharArray(data.json, 100);
      data.valid = true;
      return 0;
   }

   /// <summary>
   /// Reads a stored string value.
   /// </summary>
   String getString(const char* key, String defaultValue = String())
   {
      (void)key;
      if (data.valid)
      {
         return data.json;
      }

      return defaultValue;
   }

   /// <summary>
   /// Removes a stored key.
   /// </summary>
   bool remove(const char* key)
   {
      (void)key;
      data.valid = false;
      return true;
   }
};

/// <summary>
/// Feather M0 OLED board wrapper with buttons and flash-backed preferences.
/// </summary>
class Feather_M0_OLED : public ArduinoWithDisplay
{
public:
   SW_SH1107 display;
   Button buttonA;
   Button buttonB;
   Button buttonC;
   PreferencesFlash preferences;

   Feather_M0_OLED() : ArduinoWithDisplay(&display, &display), display(64, 128, &Wire), buttonA(9), buttonB(6), buttonC(5)
   {
   }

   /// <summary>
   /// Initializes buttons and display hardware.
   /// </summary>
   void begin()
   {
      buttonA.begin();
      buttonB.begin();
      buttonC.begin();

      display.begin(0x3C, true);
      display.setTextColor((uint16_t)Color::WHITE);
      display.setRotation(1);
      display.clearDisplay();
      display.display();
   }

   /// <summary>
   /// Clears the display and resets the cursor to (0,0).
   /// </summary>
   virtual void clearDisplay()
   {
      display.clearDisplay();
      display.setCursor(0, 0);
   }

   /// <summary>
   /// Flushes buffered display changes to the panel.
   /// </summary>
   void displayDisplay()
   {
      display.display();
   }
};
