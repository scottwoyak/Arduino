
#include <Feather.h>
#include <SerialX.h>

Feather feather;
constexpr auto TEXT_SIZE = 5;

void setup()
{
   SerialX::begin();
   feather.begin();


   Serial.println("MONOSPACED");
   feather.setTextSize(TEXT_SIZE, true);
   printCharWidths();

   Serial.println("PROPORTIONAL");
   feather.setTextSize(TEXT_SIZE, false);
   printCharWidths();
}

void printCharWidths()
{
   // unsafe cast
   const lgfx::v1::VLWfont* font = (const lgfx::v1::VLWfont*)feather.display.getFont();

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
      Serial.print(feather.display.textWidth(String(c)));
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
      feather.print(String(str[i]), Color::WHITE, c1);
      charPositions.push_back(feather.display.getCursorX());
      std::swap(c1, c2);
   }
   feather.println();

   c1 = Color::YELLOW;
   c2 = Color::CYAN;
   feather.setTextSize(2, false);
   for (size_t i = 0; i < charPositions.size(); i++)
   {
      int charW = charPositions[i] - (i > 0 ? charPositions[i - 1] : 0);
      feather.print(charW, c1);
      std::swap(c1, c2);
      feather.print(" ");
   }

   feather.println();
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
   if (feather.buttonA.wasPressed())
   {
      strIndex++;
      if (strIndex == TEST_STRINGS.size())
      {
         strIndex = 0;
      }

      feather.clearDisplay();
   }

   feather.setCursor(0, 0);
   feather.setTextSize(TEXT_SIZE, true);
   colorPrintln(TEST_STRINGS[strIndex]);
   feather.moveCursorY(10);

   feather.setTextSize(TEXT_SIZE, false);
   colorPrintln(TEST_STRINGS[strIndex]);
}


