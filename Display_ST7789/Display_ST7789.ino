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

long lastMicros = micros();

// Add the main program code into the continuous loop() function
void loop()
{
   long newMicros = micros();
   double fps = 1000000.0 / (newMicros - lastMicros);

   display.setTextSize(4);
   display.setCursor(0, 0);
   display.println(random(9999));
   display.println(random(9999));
   display.println(random(9999));

   display.setTextSize(2);
   display.setCursor(0, display.height() - 16);
   display.print(fps, 1);
   display.print(" fps ");

   lastMicros = newMicros;
}
