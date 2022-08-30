/**************************************************************************
  This is a library for several Adafruit displays based on ST77* drivers.

  Works with the Adafruit ESP32-S2 TFT Feather
    ----> http://www.adafruit.com/products/5300

  Check out the links above for our tutorials and wiring diagrams.

  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
 **************************************************************************/

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>

 // Use dedicated hardware SPI pins
Adafruit_ST7789 display = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

float p = 3.1415926;

void setup(void) {
   Serial.begin(9600);
   Serial.print(F("Hello! Feather TFT Test"));

   // turn on backlite
   pinMode(TFT_BACKLITE, OUTPUT);
   digitalWrite(TFT_BACKLITE, HIGH);

   // turn on the TFT / I2C power supply
   pinMode(TFT_I2C_POWER, OUTPUT);
   digitalWrite(TFT_I2C_POWER, HIGH);
   delay(10);

   // initialize TFT
   display.init(135, 240); // Init ST7789 240x135
   display.setRotation(3);
   display.fillScreen(ST77XX_BLACK);

   Serial.println(F("Initialized"));

   uint16_t time = millis();
   display.fillScreen(ST77XX_BLACK);
   time = millis() - time;

   Serial.println(time, DEC);
   delay(500);

   // large block of text
   display.fillScreen(ST77XX_BLACK);
   testdrawtext(
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Curabitur "
      "adipiscing ante sed nibh tincidunt feugiat. Maecenas enim massa, "
      "fringilla sed malesuada et, malesuada sit amet turpis. Sed porttitor "
      "neque ut ante pretium vitae malesuada nunc bibendum. Nullam aliquet "
      "ultrices massa eu hendrerit. Ut sed nisi lorem. In vestibulum purus a "
      "tortor imperdiet posuere. ",
      ST77XX_WHITE);
   delay(1000);

   // tft print function!
   tftPrintTest();
   delay(4000);

   // a single pixel
   display.drawPixel(display.width() / 2, display.height() / 2, ST77XX_GREEN);
   delay(500);

   // line draw test
   testlines(ST77XX_YELLOW);
   delay(500);

   // optimized lines
   testfastlines(ST77XX_RED, ST77XX_BLUE);
   delay(500);

   testdrawrects(ST77XX_GREEN);
   delay(500);

   testfillrects(ST77XX_YELLOW, ST77XX_MAGENTA);
   delay(500);

   display.fillScreen(ST77XX_BLACK);
   testfillcircles(10, ST77XX_BLUE);
   testdrawcircles(10, ST77XX_WHITE);
   delay(500);

   testroundrects();
   delay(500);

   testtriangles();
   delay(500);

   mediabuttons();
   delay(500);

   Serial.println("done");
   delay(1000);
}

void loop() {
   display.invertDisplay(true);
   delay(500);
   display.invertDisplay(false);
   delay(500);
}

void testlines(uint16_t color) {
   display.fillScreen(ST77XX_BLACK);
   for (int16_t x = 0; x < display.width(); x += 6) {
      display.drawLine(0, 0, x, display.height() - 1, color);
      delay(0);
   }
   for (int16_t y = 0; y < display.height(); y += 6) {
      display.drawLine(0, 0, display.width() - 1, y, color);
      delay(0);
   }

   display.fillScreen(ST77XX_BLACK);
   for (int16_t x = 0; x < display.width(); x += 6) {
      display.drawLine(display.width() - 1, 0, x, display.height() - 1, color);
      delay(0);
   }
   for (int16_t y = 0; y < display.height(); y += 6) {
      display.drawLine(display.width() - 1, 0, 0, y, color);
      delay(0);
   }

   display.fillScreen(ST77XX_BLACK);
   for (int16_t x = 0; x < display.width(); x += 6) {
      display.drawLine(0, display.height() - 1, x, 0, color);
      delay(0);
   }
   for (int16_t y = 0; y < display.height(); y += 6) {
      display.drawLine(0, display.height() - 1, display.width() - 1, y, color);
      delay(0);
   }

   display.fillScreen(ST77XX_BLACK);
   for (int16_t x = 0; x < display.width(); x += 6) {
      display.drawLine(display.width() - 1, display.height() - 1, x, 0, color);
      delay(0);
   }
   for (int16_t y = 0; y < display.height(); y += 6) {
      display.drawLine(display.width() - 1, display.height() - 1, 0, y, color);
      delay(0);
   }
}

void testdrawtext(char* text, uint16_t color) {
   display.setCursor(0, 0);
   display.setTextColor(color);
   display.setTextWrap(true);
   display.print(text);
}

void testfastlines(uint16_t color1, uint16_t color2) {
   display.fillScreen(ST77XX_BLACK);
   for (int16_t y = 0; y < display.height(); y += 5) {
      display.drawFastHLine(0, y, display.width(), color1);
   }
   for (int16_t x = 0; x < display.width(); x += 5) {
      display.drawFastVLine(x, 0, display.height(), color2);
   }
}

void testdrawrects(uint16_t color) {
   display.fillScreen(ST77XX_BLACK);
   for (int16_t x = 0; x < display.width(); x += 6) {
      display.drawRect(display.width() / 2 - x / 2, display.height() / 2 - x / 2, x, x,
         color);
   }
}

void testfillrects(uint16_t color1, uint16_t color2) {
   display.fillScreen(ST77XX_BLACK);
   for (int16_t x = display.width() - 1; x > 6; x -= 6) {
      display.fillRect(display.width() / 2 - x / 2, display.height() / 2 - x / 2, x, x,
         color1);
      display.drawRect(display.width() / 2 - x / 2, display.height() / 2 - x / 2, x, x,
         color2);
   }
}

void testfillcircles(uint8_t radius, uint16_t color) {
   for (int16_t x = radius; x < display.width(); x += radius * 2) {
      for (int16_t y = radius; y < display.height(); y += radius * 2) {
         display.fillCircle(x, y, radius, color);
      }
   }
}

void testdrawcircles(uint8_t radius, uint16_t color) {
   for (int16_t x = 0; x < display.width() + radius; x += radius * 2) {
      for (int16_t y = 0; y < display.height() + radius; y += radius * 2) {
         display.drawCircle(x, y, radius, color);
      }
   }
}

void testtriangles() {
   display.fillScreen(ST77XX_BLACK);
   uint16_t color = 0xF800;
   int t;
   int w = display.width() / 2;
   int x = display.height() - 1;
   int y = 0;
   int z = display.width();
   for (t = 0; t <= 15; t++) {
      display.drawTriangle(w, y, y, x, z, x, color);
      x -= 4;
      y += 4;
      z -= 4;
      color += 100;
   }
}

void testroundrects() {
   display.fillScreen(ST77XX_BLACK);
   uint16_t color = 100;
   int i;
   int t;
   for (t = 0; t <= 4; t += 1) {
      int x = 0;
      int y = 0;
      int w = display.width() - 2;
      int h = display.height() - 2;
      for (i = 0; i <= 16; i += 1) {
         display.drawRoundRect(x, y, w, h, 5, color);
         x += 2;
         y += 3;
         w -= 4;
         h -= 6;
         color += 1100;
      }
      color += 100;
   }
}

void tftPrintTest() {
   display.setTextWrap(false);
   display.fillScreen(ST77XX_BLACK);
   display.setCursor(0, 30);
   display.setTextColor(ST77XX_RED);
   display.setTextSize(1);
   display.println("Hello World!");
   display.setTextColor(ST77XX_YELLOW);
   display.setTextSize(2);
   display.println("Hello World!");
   display.setTextColor(ST77XX_GREEN);
   display.setTextSize(3);
   display.println("Hello World!");
   display.setTextColor(ST77XX_BLUE);
   display.setTextSize(4);
   display.print(1234.567);
   delay(1500);
   display.setCursor(0, 0);
   display.fillScreen(ST77XX_BLACK);
   display.setTextColor(ST77XX_WHITE);
   display.setTextSize(0);
   display.println("Hello World!");
   display.setTextSize(1);
   display.setTextColor(ST77XX_GREEN);
   display.print(p, 6);
   display.println(" Want pi?");
   display.println(" ");
   display.print(8675309, HEX); // print 8,675,309 out in HEX!
   display.println(" Print HEX!");
   display.println(" ");
   display.setTextColor(ST77XX_WHITE);
   display.println("Sketch has been");
   display.println("running for: ");
   display.setTextColor(ST77XX_MAGENTA);
   display.print(millis() / 1000);
   display.setTextColor(ST77XX_WHITE);
   display.print(" seconds.");
}

void mediabuttons() {
   // play
   display.fillScreen(ST77XX_BLACK);
   display.fillRoundRect(25, 5, 78, 60, 8, ST77XX_WHITE);
   display.fillTriangle(42, 12, 42, 60, 90, 40, ST77XX_RED);
   delay(500);
   // pause
   display.fillRoundRect(25, 70, 78, 60, 8, ST77XX_WHITE);
   display.fillRoundRect(39, 78, 20, 45, 5, ST77XX_GREEN);
   display.fillRoundRect(69, 78, 20, 45, 5, ST77XX_GREEN);
   delay(500);
   // play color
   display.fillTriangle(42, 12, 42, 60, 90, 40, ST77XX_BLUE);
   delay(50);
   // pause color
   display.fillRoundRect(39, 78, 20, 45, 5, ST77XX_RED);
   display.fillRoundRect(69, 78, 20, 45, 5, ST77XX_RED);
   // play color
   display.fillTriangle(42, 12, 42, 60, 90, 40, ST77XX_GREEN);
}
