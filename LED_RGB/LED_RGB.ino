#include <LED.h>
#include <Stopwatch.h>
#include <SerialX.h>

constexpr uint8_t RED_PIN = 9;
constexpr uint8_t GREEN_PIN = 6;
constexpr uint8_t BLUE_PIN = 5;

Stopwatch sw;

enum class Color
{
   Red,
   Yellow,
   Green,
   Cyan,
   Blue,
   Magenta,
   White,
   Count,
} mode;

Color operator++(Color& color, int)
{
   color = static_cast<Color>((static_cast<int>(color) + 1) % static_cast<int>(Color::Count));
   return color;
}

Color color = Color::Red;

RGBLED led(RED_PIN, GREEN_PIN, BLUE_PIN);

void setup()
{
   Serial.begin(115200);
   SerialX::begin();

   led.begin();
   led.turnOn();
}

void loop()
{
   if (sw.elapsedMillis() > 1000)
   {
      sw.reset();
      color++;
   }

   switch (color)
   {
   case Color::Red:
      led.setColor(1.0f, 0.0f, 0.0f);
      break;
   case Color::Yellow:
      led.setColor(1.0f, 1.0f, 0.0f);
      break;
   case Color::Green:
      led.setColor(0.0f, 1.0f, 0.0f);
      break;
   case Color::Cyan:
      led.setColor(0.0f, 1.0f, 1.0f);
      break;
   case Color::Blue:
      led.setColor(0.0f, 0.0f, 1.0f);
      break;
   case Color::Magenta:
      led.setColor(1.0f, 0.0f, 1.0f);
      break;
   case Color::White:
      led.setColor(1.0f, 1.0f, 1.0f);
      break;
   }

   delay(1000);
}
