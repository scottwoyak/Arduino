
#include <Feather.h>
#include <SerialX.h>

Feather feather;

Format percentFormat("###%");
Format batteryVoltsFormat("#.#v");
Format chargeRateFormat("####%/hr");

void setup()
{
   SerialX::begin();

   feather.begin();
}

uint8_t view = 0;
constexpr auto NUM_VIEWS = 7;

void toSerial(const char* format, Format& f)
{
   SerialX::println("Format '", format, "'");
   SerialX::println("    precision: ", String(f.precision).c_str());
   SerialX::println("       prefix: '", f.prefix.c_str(), "'");
   SerialX::println("      postfix: '", f.postfix.c_str(), "'");
   SerialX::println("      errChar: ", String(f.errChar).c_str());
   SerialX::println("  includePlus: ", f.includePlus ? "true" : "false");
   SerialX::println("       length: ", String(f.length).c_str());
   const char* alignment = "unknown";
   switch (f.alignment)
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
   SerialX::println("    alignment: ", alignment);
   SerialX::println();
}
void println(const char* format, double value)
{
   feather.print(format, Color::LABEL, Color::DARKGRAY);
   feather.setCursorX(feather.display.width() / 2);
   Format f(format);
   toSerial(format, f);
   feather.println(value, f, Color::VALUE, Color::DARKGRAY);
}

void println2(const char* format, double value)
{
   feather.print(value, 4, Color::LABEL, Color::DARKGRAY);
   feather.setCursorX(feather.display.width() / 2);
   Format f(format);
   toSerial(format, f);
   feather.println(value, f, Color::VALUE, Color::DARKGRAY);
}

void println(const char* format, size_t length, double value)
{
   feather.print(format, Color::LABEL, Color::DARKGRAY);
   feather.setCursorX(feather.display.width() / 2);
   Format f(format);
   f.length = length;
   toSerial(format, f);
   feather.println(value, f, Color::VALUE, Color::DARKGRAY);
}

void println(const char* format, Format::Alignment alignment, double value)
{
   feather.print(format, Color::LABEL, Color::DARKGRAY);
   feather.setCursorX(feather.display.width() / 2);
   Format f(format);
   f.alignment = alignment;
   toSerial(format, f);
   feather.println(value, f, Color::VALUE, Color::DARKGRAY);
}

bool first = true;
void loop()
{
   if (feather.buttonA.wasPressed())
   {
      view++;
      feather.clearDisplay();
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

   feather.setCursor(0, 0);
   feather.setTextSize(3);

   switch (view % NUM_VIEWS)
   {
   case 0:
   {
      feather.println("Precision", Color::HEADING);
      feather.moveCursorY(feather.charH() / 2);

      feather.setTextSize(2);
      float value = 3.141592;
      println("#", value);
      println("#.#", value);
      println("#.##", value);
      println("#.###", value);
   }
   break;

   case 1:
   {
      feather.println("Prefix", Color::HEADING);
      feather.moveCursorY(feather.charH() / 2);

      feather.setTextSize(2);
      float value = 3.141592;
      println("PI=#.##", value);
      println("   #.##", value);
   }
   break;

   case 2:
   {
      feather.println("Postfix", Color::HEADING);
      feather.moveCursorY(feather.charH() / 2);

      feather.setTextSize(2);
      float value = 3.141592;
      println("#s", value);
      println("#.# s", value);
      println("#.##m/s", value);
      println("#.##   ", value);
   }
   break;

   case 3:
   {
      feather.println("errChar", Color::HEADING);
      feather.moveCursorY(feather.charH() / 2);

      feather.setTextSize(2);
      float value = 123.4;
      feather.print("value=", Color::LABEL);
      feather.print(value, 1, Color::LABEL);
      feather.println();

      println("#.#", value);
      println("##.#", value);
      println("###.#", value);
      println("####.#", value);
      println("#####.#", value);
   }
   break;

   case 4:
   {
      feather.println("includePlus", Color::HEADING);
      feather.moveCursorY(feather.charH() / 2);

      feather.setTextSize(2);
      feather.print("format ", Color::VALUE);
      const char* format = "+#.##";
      feather.print(format, Color::VALUE, Color::DARKGRAY);
      feather.println();

      println2(format, 0.5);
      println2(format, -0.5);
      println2(format, 0);
      println2(format, 0.001);
      println2(format, -0.001);
   }
   break;

   case 5:
   {
      feather.println("length", Color::HEADING);
      feather.moveCursorY(feather.charH() / 2);

      feather.setTextSize(2);
      float value = 3.141592;
      feather.print("2 ", Color::LABEL);
      println("#.##", 2, value);
      feather.print("3 ", Color::LABEL);
      println("#.##", 3, value);
      feather.print("4 ", Color::LABEL);
      println("#.##", 4, value);
      feather.print("5 ", Color::LABEL);
      println("#.##", 5, value);
      feather.print("6 ", Color::LABEL);
      println("#.##", 6, value);
   }
   break;

   case 6:
   {
      feather.println("alignment", Color::HEADING);
      feather.moveCursorY(feather.charH() / 2);

      feather.setTextSize(2);
      float value = 3.141592;
      feather.println("Left", Color::LABEL);
      println("#####.##", Format::Alignment::LEFT, value);
      feather.println("Center", Color::LABEL);
      println("#####.##", Format::Alignment::CENTER, value);
      feather.println("Right", Color::LABEL);
      println("#####.##", Format::Alignment::RIGHT, value);
   }
   break;

   }
}
