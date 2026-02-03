#include <Adafruit_ST7796S.h> // TFT display

// if any of the TFT defines are not found, it is because the correct board
// is not selected. Needs to be Adafruit Feather ESP32-S3
constexpr auto PIN_TFT_BACKLITE = 5;
constexpr auto PIN_DC = 6;
constexpr auto PIN_RST = 9;
constexpr auto PIN_CS = 10;
Adafruit_ST7796S display(PIN_CS, PIN_DC, PIN_RST);

// The setup() function runs once each time the micro-controller starts
void setup()
{
   // initialize TFT
   pinMode(PIN_TFT_BACKLITE, OUTPUT);
   digitalWrite(PIN_TFT_BACKLITE, HIGH);

   // turn on the TFT / I2C power supply
   pinMode(TFT_I2C_POWER, OUTPUT);
   digitalWrite(TFT_I2C_POWER, HIGH);
   delay(10);

   display.init(320, 480, 0, 0, ST7796S_BGR);
   display.setRotation(0);
   display.invertDisplay(true);
   display.fillScreen(ST77XX_BLACK);
   display.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
}

unsigned long counter = 0;
unsigned long lastMicros = micros();

// Add the main program code into the continuous loop() function
void loop()
{
   unsigned long newMicros = micros();
   double fps = 1000000.0 / (newMicros - lastMicros);

   display.setTextSize(3);
   display.setCursor(0, 0);

   // note: this display isn't double buffered. Past contents are not cleared,
   // but text characters overwrite past content. That's why there is an extra
   // space after "fps" - some rates are over 100 and some are 2 digits.
   display.print(counter++);
   display.setTextSize(1);
   display.setCursor(0, display.height() - 8);
   display.print(fps);
   display.print(" fps ");

   lastMicros = newMicros;
}
