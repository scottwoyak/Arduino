
#include "Feather.h"

Feather feather;

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
   feather.begin();
}

void printRange(uint8_t min, uint8_t max)
{
   if (min == max)
   {
      feather.print(min, Color::VALUE);
   }
   else
   {
      feather.print(min, Color::VALUE);
      feather.print("-", Color::VALUE);
      feather.print(max, Color::VALUE);
   }
}

void computeMetrics(FontMetrics* m, bool print = false)
{
   m->minAdvance = 255;
   m->maxAdvance = 0;
   m->minWidth = 255;
   m->maxWidth = 0;
   m->height = feather.display.gFont.yAdvance;
   for (uint16_t i = 0; i < feather.display.gFont.gCount; i++)
   {
      m->minAdvance = std::min(m->minAdvance, feather.display.gxAdvance[i]);
      m->maxAdvance = std::max(m->maxAdvance, feather.display.gxAdvance[i]);
      m->minWidth = std::min(m->minWidth, feather.display.gWidth[i]);
      m->maxWidth = std::max(m->maxWidth, feather.display.gWidth[i]);

      char c = feather.display.gUnicode[i] < 256 ? (char)feather.display.gUnicode[i] : '\0';
      m->printMinWidth = std::min(m->printMinWidth, (uint8_t)feather.display.textWidth(String(c)));
      m->printMaxWidth = std::max(m->printMaxWidth, (uint8_t)feather.display.textWidth(String(c)));

      if (print)
      {
         Serial.print((int)c);
         Serial.print(" '");
         Serial.print(c);
         Serial.print("'");
         Serial.print("\t");

         Serial.print(" gxAdvance:");
         Serial.print(feather.display.gxAdvance[i]);
         Serial.print("\t");

         Serial.print(" gdX:");
         Serial.print(feather.display.gdX[i]);
         Serial.print("\t");

         Serial.print(" gWidth:");
         Serial.print(feather.display.gWidth[i]);
         Serial.print("\t");

         Serial.print(" textwidth():");
         Serial.print(feather.display.textWidth(String(c)));
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
   if (feather.buttonA.wasPressed())
   {
      feather.clearDisplay();
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
      feather.setTextSize(textSize, mono);
      FontMetrics fm;
      computeMetrics(&fm, true);

      feather.setCursor(0, 0);
      feather.setTextSize(3, mono);
      uint16_t lineHeight = feather.charH();
      feather.print("Size: ", Color::LABEL);
      feather.print(textSize, Color::LABEL);
      feather.print(" ", Color::LABEL);
      feather.print(fm.height, Color::VALUE);
      feather.print("px, ", Color::VALUE);
      feather.print(mono ? "mono" : "prop", Color::VALUE);
      feather.println();

      feather.setTextSize(textSize, mono);
      uint16_t top = lineHeight;
      uint16_t bottom = feather.height() - 2 * lineHeight;
      feather.setCursor(0, top + (bottom - top - feather.charH()) / 2.0);
      feather.printlnC("ABC.xyz", Color::ORANGE);

      feather.setCursor(0, bottom);
      feather.setTextSize(3, mono);

      feather.print("  xAdvance: ", Color::LABEL);
      printRange(fm.minAdvance, fm.maxAdvance);
      feather.println();

      feather.print("Char Width: ", Color::LABEL);
      printRange(fm.minWidth, fm.maxWidth);
   }
}


