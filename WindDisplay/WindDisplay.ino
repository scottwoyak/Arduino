
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
#include <TimedHistogram.h>
#include <WindMeter.h>
#include <History.h>

// Pins
#define WIND_PIN A0

#define TOTAL_TIME 10*60*1000
#define NUM_BINS 30
#define MAX_SPEED 10.0
#define FONT_HEIGHT 16
#define FONT_WIDTH 12
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 135

Adafruit_ST7789 display(TFT_CS, TFT_DC, TFT_RST);
TimedHistogram hg(0, MAX_SPEED, NUM_BINS, TOTAL_TIME);
WindMeter windMeter(WIND_PIN);
History history(DISPLAY_WIDTH - 2);


void setup(void) {

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
      ;
   delay(500);

   windMeter.begin();

   // turn on backlite
   pinMode(TFT_BACKLITE, OUTPUT);
   digitalWrite(TFT_BACKLITE, HIGH);

   // turn on the TFT / I2C power supply
   pinMode(TFT_I2C_POWER, OUTPUT);
   digitalWrite(TFT_I2C_POWER, HIGH);
   delay(10);

   // initialize TFT
   display.init(135, 240); // Init ST7789 240x135
   display.setRotation(2);
   display.fillScreen(ST77XX_BLACK);

   display.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
   display.setTextSize(2);

   /*
   display.setCursor(0, 0);
   display.print(0.0, 0);

   display.setCursor(0, (1 / 4.0) * DISPLAY_WIDTH - FONT_HEIGHT / 2);
   display.print(1.0 * MAX_SPEED / 4, 0);

   display.setCursor(0, (2 / 4.0) * DISPLAY_WIDTH - FONT_HEIGHT / 2);
   display.print(2.0 * MAX_SPEED / 4, 0);

   display.setCursor(0, (3 / 4.0) * DISPLAY_WIDTH - FONT_HEIGHT / 2);
   display.print(3.0 * MAX_SPEED / 4, 0);

   display.setCursor(0, DISPLAY_WIDTH - FONT_HEIGHT);
   display.print(MAX_SPEED, 0);
*/

   uint numValues = 6;
   for (uint i = 0; i < numValues; i++) {
      uint y;
      if (i == 0) {
         y = 0;
      }
      else if (i == numValues - 1) {
         y = DISPLAY_WIDTH - FONT_HEIGHT;
      }
      else {
         y = (i / (numValues - 1.0)) * DISPLAY_WIDTH - FONT_HEIGHT / 2;
      }

      float value = i * MAX_SPEED / (numValues - 1.0);

      uint x;
      if (value < 10) {
         x = FONT_WIDTH;
      }
      else {
         x = 0;
      }

      display.setCursor(x, y);
      display.print(value, 0);
   }
}

void drawHistogram(Adafruit_GFX* gfx, float bins[], uint numBins, uint x, uint y, uint width, uint height) {

   uint spacing = 3;
   uint barWidth = (width + spacing) / numBins;

   for (uint i = 0; i < numBins; i++) {
      uint split = bins[i] * height;
      gfx->fillRect(x, y + (height - split), barWidth - spacing, split, ST77XX_GREEN);
      gfx->fillRect(x, y, barWidth - spacing, height - split - 1, ST77XX_BLACK);
      x += barWidth;
   }
}

void drawGraph(
   Adafruit_GFX* gfx,
   History* history,
   float maxValue,
   uint x,
   uint y,
   uint width,
   uint height
) {
   uint16_t GRAY = 0x8410;
   gfx->drawRect(x, y, width, height, GRAY);

   x += 1;
   y += 1;
   width -= 2;
   height -= 2;

   uint bottomRow = y + height - 1;

   for (uint i = 0; i < width; i++) {
      uint px = x + width - (i + 1);
      uint py = bottomRow - (height - 1) * (history->get(i) / maxValue);
      gfx->drawLine(px, y, px, py, ST77XX_BLACK);
      gfx->drawLine(px, py, px, y + height - 1, ST77XX_GREEN);
   }
}

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
   return (((r & 0b11111000) << 8) + ((g & 0b11111100) << 3) + (b >> 3));
}

void pprint(Adafruit_GFX* gfx, float value) {
   if (value < 9.99) {
      gfx->print(value, 2);
   }
   else if (value < 99.9) {
      gfx->print(value, 1);
   }
   else {
      gfx->print(value, 0);
      gfx->print(' ');
   }
}

float lastMax = 0;
Stopwatch sw;
void loop() {

   float current = windMeter.getCurrent();
   current = (MAX_SPEED / 2) * (1 + sin(millis() / 1000.0));
   current = MAX_SPEED * (random(0, 1000000) / 1000000.0);

   history.set(current);

   // get values that involve computation
   hg.set(current);
   float values[NUM_BINS];
   hg.get(values);
   float min = hg.min();
   float max = hg.max();

   display.setRotation(3);

   // draw the gui
   uint MARGIN = 3;
   uint x = 0;
   uint y = FONT_HEIGHT + MARGIN;
   uint w = DISPLAY_WIDTH;
   uint h = DISPLAY_HEIGHT - 2 * FONT_WIDTH - FONT_HEIGHT - 2 * MARGIN;
   drawHistogram(&display, values, NUM_BINS, x, y, w, h);
   //drawGraph(&display, &history, MAX_SPEED, x, y, w, h);

   // draw the min, max, and current
   display.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
   display.setTextSize(2);
   display.setCursor(DISPLAY_WIDTH - 19 * FONT_WIDTH, 0);
   pprint(&display, min);

   display.setCursor(DISPLAY_WIDTH - 12 * FONT_WIDTH, 0);
   pprint(&display, current);

   display.setCursor(DISPLAY_WIDTH - 5 * FONT_WIDTH, 0);
   if (max != lastMax) {
      sw.reset();
   }

   if (sw.elapsedMillis() < 1000) {
      display.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
   }
   else {
      display.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
   }

   pprint(&display, max);

   lastMax = max;
}

