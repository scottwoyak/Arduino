
#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "SerialX.h"

Arduino arduino;

Format percentFormat("###%");
Format batteryVoltsFormat("#.#v");
Format chargeRateFormat("####%/hr");

void setup()
{
   SerialX::begin();

   arduino.begin();
}

uint8_t view = 0;
constexpr auto NUM_VIEWS = 7;

void toSerial(const char* format, Format& f)
{
   Serial.print("Format '");
   Serial.print(format);
   Serial.println("'");

   Serial.print("    precision: ");
   Serial.println(f.precision());

   Serial.print("       prefix: '");
   Serial.print(f.prefix().c_str());
   Serial.println("'");

   Serial.print("      postfix: '");
   Serial.print(f.postfix().c_str());
   Serial.println("'");

   Serial.print("      errChar: ");
   Serial.println(f.errChar());

   Serial.print("  includePlus: ");
   Serial.println(f.includePlus() ? "true" : "false");

   Serial.print("       length: ");
   Serial.println(f.length());

   const char* alignment = "unknown";
   switch (f.alignment())
   {
   case Format::Alignment::LEFT:
      alignment = "Left";
      break;

   case Format::Alignment::CENTER:
      alignment = "Center";
      break;

   case Format::Alignment::RIGHT:
      alignment = "Right";
      break;
   }

   Serial.print("    alignment: ");
   Serial.println(alignment);
   Serial.println();
}
void println(const char* format, double value)
{
   arduino.print(format, Color::LABEL, Color::DARKGRAY);
   arduino.setCursorX(arduino.display.width() / 2);
   Format f(format);
   toSerial(format, f);
   arduino.println(value, f, Color::VALUE, Color::DARKGRAY);
}

void println2(const char* format, double value)
{
   arduino.print(value, 4, Color::LABEL, Color::DARKGRAY);
   arduino.setCursorX(arduino.display.width() / 2);
   Format f(format);
   toSerial(format, f);
   arduino.println(value, f, Color::VALUE, Color::DARKGRAY);
}

void println(const char* format, size_t length, double value)
{
   arduino.print(format, Color::LABEL, Color::DARKGRAY);
   arduino.setCursorX(arduino.display.width() / 2);
   Format f(format, length);
   toSerial(format, f);
   arduino.println(value, f, Color::VALUE, Color::DARKGRAY);
}

void println(const char* format, Format::Alignment alignment, double value)
{
   arduino.print(format, Color::LABEL, Color::DARKGRAY);
   arduino.setCursorX(arduino.display.width() / 2);
   Format f(format, alignment);
   toSerial(format, f);
   arduino.println(value, f, Color::VALUE, Color::DARKGRAY);
}

bool first = true;
void loop()
{
   if (arduino.buttonA.wasPressed())
   {
      view++;
      arduino.clearDisplay();
   }
   else
   {
      if (first)
      {
         first = false;
      }
      else
      {
         return;
      }
   }

   arduino.setCursor(0, 0);
   arduino.setTextSize(3);

   switch (view % NUM_VIEWS)
   {
   case 0:
   {
      arduino.println("Precision", Color::HEADING);
      arduino.moveCursorY(arduino.charH() / 2);

      arduino.setTextSize(2);
      float value = 3.141592;
      println("#", value);
      println("#.#", value);
      println("#.##", value);
      println("#.###", value);
   }
   break;

   case 1:
   {
      arduino.println("Prefix", Color::HEADING);
      arduino.moveCursorY(arduino.charH() / 2);

      arduino.setTextSize(2);
      float value = 3.141592;
      println("PI=#.##", value);
      println("   #.##", value);
   }
   break;

   case 2:
   {
      arduino.println("Postfix", Color::HEADING);
      arduino.moveCursorY(arduino.charH() / 2);

      arduino.setTextSize(2);
      float value = 3.141592;
      println("#s", value);
      println("#.# s", value);
      println("#.##m/s", value);
      println("#.##   ", value);
   }
   break;

   case 3:
   {
      arduino.println("errChar", Color::HEADING);
      arduino.moveCursorY(arduino.charH() / 2);

      arduino.setTextSize(2);
      float value = 123.4;
      arduino.print("value=", Color::LABEL);
      arduino.print(value, 1, Color::LABEL);
      arduino.println();

      println("#.#", value);
      println("##.#", value);
      println("###.#", value);
      println("####.#", value);
      println("#####.#", value);
   }
   break;

   case 4:
   {
      arduino.println("includePlus", Color::HEADING);
      arduino.moveCursorY(arduino.charH() / 2);

      arduino.setTextSize(2);
      arduino.print("format ", Color::VALUE);
      const char* format = "+#.##";
      arduino.print(format, Color::VALUE, Color::DARKGRAY);
      arduino.println();

      println2(format, 0.5);
      println2(format, -0.5);
      println2(format, 0);
      println2(format, 0.001);
      println2(format, -0.001);
   }
   break;

   case 5:
   {
      arduino.println("length", Color::HEADING);
      arduino.moveCursorY(arduino.charH() / 2);

      arduino.setTextSize(2);
      float value = 3.141592;
      arduino.print("2 ", Color::LABEL);
      println("#.##", 2, value);
      arduino.print("3 ", Color::LABEL);
      println("#.##", 3, value);
      arduino.print("4 ", Color::LABEL);
      println("#.##", 4, value);
      arduino.print("5 ", Color::LABEL);
      println("#.##", 5, value);
      arduino.print("6 ", Color::LABEL);
      println("#.##", 6, value);
   }
   break;

   case 6:
   {
      arduino.println("alignment", Color::HEADING);
      arduino.moveCursorY(arduino.charH() / 2);

      arduino.setTextSize(2);
      float value = 3.141592;
      arduino.println("Left", Color::LABEL);
      println("#####.##", Format::Alignment::LEFT, value);
      arduino.println("Center", Color::LABEL);
      println("#####.##", Format::Alignment::CENTER, value);
      arduino.println("Right", Color::LABEL);
      println("#####.##", Format::Alignment::RIGHT, value);
   }
   break;

   }
}
