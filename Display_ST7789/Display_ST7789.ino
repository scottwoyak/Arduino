#include <Adafruit_ST7789.h> // TFT display

// if any of the TFT defines are not found, it is because the correct board
// is not selected. Needs to be Adafruit Feather ESP32-S3
Adafruit_ST7789 display(TFT_CS, TFT_DC, TFT_RST);

// The setup() function runs once each time the micro-controller starts
void setup()
{
   // initialize TFT
   pinMode(TFT_BACKLITE, OUTPUT);
   digitalWrite(TFT_BACKLITE, HIGH);

   // turn on the TFT / I2C power supply
   pinMode(TFT_I2C_POWER, OUTPUT);
   digitalWrite(TFT_I2C_POWER, HIGH);
   delay(10);

   display.init(135, 240); // Init ST7789 240x135
   display.setRotation(3);
   display.fillScreen(ST77XX_BLACK);
   display.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
   display.setTextSize(2);
}

long counter = 0;
long lastMicros = micros();

// Add the main program code into the continuous loop() function
void loop()
{
   long newMicros = micros();
   double fps = 1000000.0 / (newMicros - lastMicros);

   // note: this display isn't double buffered. Past contents are not cleared,
   // but text characters overwrite past content. That's why there is an extra
   // space after "fps" - some rates are over 100 and some are 2 digits.
   display.setTextSize(4);
   display.setCursor(0, 0);
   display.print(counter++);
   display.setTextSize(2);
   display.setCursor(0, display.height() - 16);
   display.print(fps);
   display.print(" fps ");

   lastMicros = newMicros;
}
