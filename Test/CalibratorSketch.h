#include <Logger.h>
#include <Util.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <I2CMultiplexor.h>
#include <RunningAverager.h>
#include <Stopwatch.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// logging mechanism
Logger Error(
   new SerialLogHandler()
   //   new DisplayLogHandler(logFeed)
);

// temperature sensors
Adafruit_SHT31 sht35_A;
Adafruit_SHT31 sht35_B;
Adafruit_SHT31 sht35_C;
Adafruit_SHT31 sht35_D;

RunningAverager avg_A(100);
RunningAverager avg_B(100);
RunningAverager avg_C(100);
RunningAverager avg_D(100);

I2CMultiplexor i2cMultiplexor;

class CalibratorSketch {
public:
   void setup() {
      // start serial port
      Serial.begin(115200);

      // wait 5 seconds for the serial monitor to open
      while (millis() < 2000 && !Serial)
         ;
      delay(500);

      Serial.println("Starting Calibration Display Sketch");

      i2cMultiplexor.begin();
      i2cMultiplexor.scan();

      i2cMultiplexor.select(2);
      sht35_A.begin(0x44);
      i2cMultiplexor.select(3);
      sht35_B.begin(0x44);
      i2cMultiplexor.select(4);
      sht35_C.begin(0x44);
      i2cMultiplexor.select(5);
      sht35_D.begin(0x44);

      // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
      if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
         Serial.println(F("SSD1306 allocation failed"));
         for (;;); // Don't proceed, loop forever
      }



      // Clear the buffer.
      display.clearDisplay();
      display.display();

      //display.setRotation(1);

      // text display tests
   //   display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
   }

   void loop() {

      i2cMultiplexor.select(2);
      float temp_A = Util::C2F(sht35_A.readTemperature());
      float humidity_A = sht35_A.readHumidity();
      i2cMultiplexor.select(3);
      float temp_B = Util::C2F(sht35_B.readTemperature());
      float humidity_B = sht35_B.readHumidity();
      i2cMultiplexor.select(4);
      float temp_C = Util::C2F(sht35_C.readTemperature());
      float humidity_C = sht35_C.readHumidity();
      i2cMultiplexor.select(5);
      float temp_D = Util::C2F(sht35_D.readTemperature());
      float humidity_D = sht35_D.readHumidity();

      avg_A.set(temp_A);
      avg_B.set(temp_B);
      avg_C.set(temp_C);
      avg_D.set(temp_D);
      float avg = (avg_A.get() + avg_B.get() + avg_C.get() + avg_D.get()) / 4;
      float diff;

      display.clearDisplay();
      display.setCursor(0, 0);

      display.print(avg_A.get(), 1);
      display.print(" ");
      diff = avg_A.get() - avg;
      if (diff > 0) display.print(" ");
      display.print(diff);
      display.println();

      display.print(avg_B.get(), 1);
      display.print(" ");
      diff = avg_B.get() - avg;
      if (diff > 0) display.print(" ");
      display.print(diff);
      display.println();

      display.print(avg_C.get(), 1);
      display.print(" ");
      diff = avg_C.get() - avg;
      if (diff > 0) display.print(" ");
      display.print(diff);
      display.println();

      display.print(avg_D.get(), 1);
      display.print(" ");
      diff = avg_D.get() - avg;
      if (diff > 0) display.print(" ");
      display.print(diff);
      display.println();


      /*
      display.print(temp_A);
      display.print(" ");
      display.print(avg_A.get());
      display.print(" ");
      display.print(humidity_A);
      display.println();

      display.print(temp_B);
      display.print(" ");
      display.print(avg_B.get());
      display.print(" ");
      display.print(humidity_B);
      display.println();

      display.print(temp_C);
      display.print(" ");
      display.print(avg_C.get());
      display.print(" ");
      display.print(humidity_C);
      display.println();

      display.print(temp_D);
      display.print(" ");
      display.print(avg_D.get());
      display.print(" ");
      display.print(humidity_D);
      display.println();
   */

      display.display();

      /*
      Serial.println("Temperature");
      i2cMultiplexor.select(2);
      Serial.print(Util::C2F(sht35_1.readTemperature()));
      Serial.print("   ");
      i2cMultiplexor.select(3);
      Serial.print(Util::C2F(sht35_2.readTemperature()));
      Serial.print("   ");
      Serial.println();

      Serial.println("Humidity");
      i2cMultiplexor.select(2);
      Serial.print(sht35_1.readHumidity());
      Serial.print("%   ");
      i2cMultiplexor.select(3);
      Serial.print(sht35_2.readHumidity());
      Serial.print("%   ");
      Serial.println();
   */

   //delay(1000);
   }
};