
#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

Arduino arduino;

uint8_t textSize = 1;

struct FontMetrics
{
   uint8_t minAdvance = 255;
   uint8_t maxAdvance = 0;
   uint8_t minWidth = 255;
   uint8_t maxWidth = 0;
   uint8_t printMinWidth = 255;
   uint8_t printMaxWidth = 0;
   uint8_t height = 0;
};


void setup()
{
   Serial.begin(115200);
   arduino.begin();
}

void printRange(uint8_t min, uint8_t max)
{
   if (min == max)
   {
      arduino.print(min, Color::VALUE);
   }
   else
   {
      arduino.print(min, Color::VALUE);
      arduino.print("-", Color::VALUE);
      arduino.print(max, Color::VALUE);
   }
}

void computeMetrics(FontMetrics* m, bool print = false)
{
   const lgfx::v1::VLWfont* font = (const lgfx::v1::VLWfont*) arduino.display.getFont();

   m->minAdvance = 255;
   m->maxAdvance = 0;
   m->minWidth = 255;
   m->maxWidth = 0;
   m->height = font->yAdvance;
   for (uint16_t i = 0; i < font->gCount; i++)
   {
      m->minAdvance = std::min(m->minAdvance, font->gxAdvance[i]);
      m->maxAdvance = std::max(m->maxAdvance, font->gxAdvance[i]);
      m->minWidth = std::min(m->minWidth, font->gWidth[i]);
      m->maxWidth = std::max(m->maxWidth, font->gWidth[i]);

      char c = font->gUnicode[i] < 256 ? (char)font->gUnicode[i] : '\0';
      m->printMinWidth = std::min(m->printMinWidth, (uint8_t)arduino.display.textWidth(String(c)));
      m->printMaxWidth = std::max(m->printMaxWidth, (uint8_t)arduino.display.textWidth(String(c)));

      if (print)
      {
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

   if (print)
   {
      Serial.print("Advance: ");
      if (m->minAdvance == m->maxAdvance)
      {
         Serial.print(m->minAdvance);
      }
      else
      {
         Serial.print(m->minAdvance);
         Serial.print("-");
         Serial.print(m->maxAdvance);
      }
      Serial.println();

      Serial.print("Width: ");
      if (m->minWidth == m->maxWidth)
      {
         Serial.print(m->minWidth);
      }
      else
      {
         Serial.print(m->minWidth);
         Serial.print("-");
         Serial.print(m->maxWidth);
      }
      Serial.println();
   }
}

bool hasChanged = true;
bool mono = true;
void loop()
{
   if (arduino.buttonA.wasPressed())
   {
      arduino.clearDisplay();
      textSize++;
      if (textSize > 7)
      {
         textSize = 1;
         mono = !mono;
      }
      hasChanged = true;
   }

   if (hasChanged)
   {
      hasChanged = false;
      arduino.setTextSize(textSize, mono);
      FontMetrics fm;
      computeMetrics(&fm, true);

      arduino.setCursor(0, 0);
      arduino.setTextSize(3, mono);
      uint16_t lineHeight = arduino.charH();
      arduino.print("Size: ", Color::LABEL);
      arduino.print(textSize, Color::LABEL);
      arduino.print(" ", Color::LABEL);
      arduino.print(fm.height, Color::VALUE);
      arduino.print("px, ", Color::VALUE);
      arduino.print(mono ? "mono" : "prop", Color::VALUE);
      arduino.println();

      arduino.setTextSize(textSize, mono);
      uint16_t top = lineHeight;
      uint16_t bottom = arduino.height() - 2 * lineHeight;
      arduino.setCursor(0, top + (bottom - top - arduino.charH()) / 2.0);
      arduino.printlnC("ABC.xyz", Color::ORANGE);

      arduino.setCursor(0, bottom);
      arduino.setTextSize(3, mono);

      arduino.print("  xAdvance: ", Color::LABEL);
      printRange(fm.minAdvance, fm.maxAdvance);
      arduino.println();

      arduino.print("Char Width: ", Color::LABEL);
      printRange(fm.minWidth, fm.maxWidth);
   }
}


