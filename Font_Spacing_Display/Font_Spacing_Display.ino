
#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "SerialX.h"

Arduino arduino;
constexpr auto TEXT_SIZE = 5;

void setup()
{
   SerialX::begin();
   arduino.begin();


   Serial.println("MONOSPACED");
   arduino.setTextSize(TEXT_SIZE, true);
   printCharWidths();

   Serial.println("PROPORTIONAL");
   arduino.setTextSize(TEXT_SIZE, false);
   printCharWidths();
}

void printCharWidths()
{
   // unsafe cast
   const lgfx::v1::VLWfont* font = (const lgfx::v1::VLWfont*)arduino.display.getFont();

   for (uint16_t i = 0; i < font->gCount; i++)
   {
      char c = font->gUnicode[i] < 256 ? (char)font->gUnicode[i] : '\0';
      Serial.print((int)c);
      Serial.print(" '");
      Serial.print(c);
      Serial.print("'");
      Serial.print("\t");

      Serial.print(" gxAdvance:");
      Serial.print(font->gxAdvance[i]);
      Serial.print("\t");

      Serial.print(" gdX:");
      Serial.print(font->gdX[i]);
      Serial.print("\t");

      Serial.print(" gWidth:");
      Serial.print(font->gWidth[i]);
      Serial.print("\t");

      Serial.print(" textwidth():");
      Serial.print(arduino.display.textWidth(String(c)));
      Serial.println();
   }
}

void colorPrintln(const char* str)
{
   Color c1 = Color565::fromRGB(0, 0, 128);
   Color c2 = Color::DARKGRAY;

   std::vector<int> charPositions;
   for (int i = 0; i < strlen(str); i++)
   {
      arduino.print(String(str[i]), Color::WHITE, c1);
      charPositions.push_back(arduino.display.getCursorX());
      std::swap(c1, c2);
   }
   arduino.println();

   c1 = Color::YELLOW;
   c2 = Color::CYAN;
   arduino.setTextSize(2, false);
   for (size_t i = 0; i < charPositions.size(); i++)
   {
      int charW = charPositions[i] - (i > 0 ? charPositions[i - 1] : 0);
      arduino.print(charW, c1);
      std::swap(c1, c2);
      arduino.print(" ");
   }

   arduino.println();
}

std::vector<const char*> TEST_STRINGS = {
   "ABC abc",
   "0123456789",
   "+-*/=.%#",
   "W @,|.0",
};

int strIndex = 0;

void loop()
{
   if (arduino.buttonA.wasPressed())
   {
      strIndex++;
      if (strIndex == TEST_STRINGS.size())
      {
         strIndex = 0;
      }

      arduino.clearDisplay();
   }

   arduino.setCursor(0, 0);
   arduino.setTextSize(TEXT_SIZE, true);
   colorPrintln(TEST_STRINGS[strIndex]);
   arduino.moveCursorY(10);

   arduino.setTextSize(TEXT_SIZE, false);
   colorPrintln(TEST_STRINGS[strIndex]);
}


