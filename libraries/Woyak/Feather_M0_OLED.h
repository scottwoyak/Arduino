#pragma once

#include <Adafruit_SH110X.h>
#include <ArduinoWithDisplay.h>
#include <Button.h>
#include <FlashStorage.h>

//
// Annoyingling, Adafruit has a lot of nifty properties in Adafruit_GFX that you can set, but not 
// get. This class is just a wrapper that exposes them
//
class SW_SH1107 : public Adafruit_SH1107
{
public:
   SW_SH1107(uint16_t w, uint16_t h, TwoWire* twi = &Wire) : Adafruit_SH1107(w, h, twi)
   {
   }

   uint8_t charW()
   {
      // 6 is the baseline character width that is scaled for larger text
      return 6 * textsize_x;
   }

   uint8_t charH()
   {
      // 8 is the baseline character width that is scaled for larger text
      return 8 * textsize_y;
   }
};


// Create a structure for storing data
typedef struct
{
   boolean valid;
   char json[100]; // TODO turn this into a more complete json structure
} FlashData;

// Reserve a portion of flash memory to store a "id" and call it "flash".
FlashStorage(flash, FlashData);

class PreferencesFlash
{
   FlashData data;

   bool begin(const char* name, bool readOnly = false)
   {
      // Read from flash
      data = flash.read();
      return true;
   }
   void end()
   {
      flash.write(data);
   }

   bool isKey(const char* key)
   {
      return data.valid;
   }
   size_t putString(const char* key, String value)
   {
      // store the information in flash
      value.toCharArray(data.json, 100);
      data.valid = true;
      return 0;
   }
   String getString(const char* key, String defaultValue = String())
   {
      // If not initialized, valid will be false
      if (data.valid)
      {
         return data.json;
      }
      else
      {
         return "";
      }
   }

   /*
        bool begin(const char * name, bool readOnly=false);
        void end();

        bool clear();
        bool remove(const char * key);

        size_t putChar(const char* key, int8_t value);
        size_t putUChar(const char* key, uint8_t value);
        size_t putShort(const char* key, int16_t value);
        size_t putUShort(const char* key, uint16_t value);
        size_t putInt(const char* key, int32_t value);
        size_t putUInt(const char* key, uint32_t value);
        size_t putLong(const char* key, int32_t value);
        size_t putULong(const char* key, uint32_t value);
        size_t putLong64(const char* key, int64_t value);
        size_t putULong64(const char* key, uint64_t value);
        size_t putFloat(const char* key, float_t value);
        size_t putDouble(const char* key, double_t value);
        size_t putBool(const char* key, bool value);
        size_t putString(const char* key, const char* value);
        size_t putString(const char* key, String value);
        size_t putBytes(const char* key, const void* buf, size_t len);

        bool isKey(const char* key);
        PreferenceType getType(const char* key);
        int8_t getChar(const char* key, int8_t defaultValue = 0);
        uint8_t getUChar(const char* key, uint8_t defaultValue = 0);
        int16_t getShort(const char* key, int16_t defaultValue = 0);
        uint16_t getUShort(const char* key, uint16_t defaultValue = 0);
        int32_t getInt(const char* key, int32_t defaultValue = 0);
        uint32_t getUInt(const char* key, uint32_t defaultValue = 0);
        int32_t getLong(const char* key, int32_t defaultValue = 0);
        uint32_t getULong(const char* key, uint32_t defaultValue = 0);
        int64_t getLong64(const char* key, int64_t defaultValue = 0);
        uint64_t getULong64(const char* key, uint64_t defaultValue = 0);
        float_t getFloat(const char* key, float_t defaultValue = NAN);
        double_t getDouble(const char* key, double_t defaultValue = NAN);
        bool getBool(const char* key, bool defaultValue = false);
        size_t getString(const char* key, char* value, size_t maxLen);
        String getString(const char* key, String defaultValue = String());
        size_t getBytesLength(const char* key);
        size_t getBytes(const char* key, void * buf, size_t maxLen);
        size_t freeEntries();
   */
};

class Feather_M0_OLED : public ArduinoWithDisplay
{
public:
   SW_SH1107 display;
   Button buttonA;
   Button buttonB;
   Button buttonC;
   PreferencesFlash preferences;

   Feather_M0_OLED() : ArduinoWithDisplay(&display), display(64, 128, &Wire), buttonA(9), buttonB(6), buttonC(5)
   {
   }

   void begin()
   {
      buttonA.begin();
      buttonB.begin();
      buttonC.begin();

      display.begin(0x3C, true); // Address 0x3C default
      display.setTextColor((uint16_t)Color::WHITE);
      display.setRotation(1);
      display.clearDisplay();
      display.display();
   }

   void displayDisplay()
   {
      display.display();
   }

   uint8_t charW()
   {
      return display.charW();
   }

   uint8_t charH()
   {
      return display.charH();
   }

   void printR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      setCursorX(-strlen(str) * charW());
      print(str, textColor, backgroundColor);
   }
   void printlnR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printR(str, textColor, backgroundColor);
      println();
   }

   void printC(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      setCursorX((display.width() - strlen(str) * charW()) / 2);
      print(str, textColor, backgroundColor);
   }
   void printlnC(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printC(str, textColor, backgroundColor);
      println();
   }
};
