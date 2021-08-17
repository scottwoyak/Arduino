#include <Logger.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Util.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// logging mechanism
Logger Error(
   new SerialLogHandler()
   //   new DisplayLogHandler(logFeed)
);

// Define Trig and Echo pin:
#define trigPin 9
#define echoPin 10

// Define variables:
long duration;
float distance;

class OLEDSR04DistanceSketch {
public:
   void setup() {
      // start serial port
      Serial.begin(115200);

      // wait 5 seconds for the serial monitor to open
      while (millis() < 2000 && !Serial)
         ;
      delay(500);

      Serial.println("Starting Sketch");

      // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
      if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
         Error.println("SSD1306 allocation failed");
         for (;;); // Don't proceed, loop forever
      }

      // Clear the buffer.
      display.clearDisplay();
      display.display();

      // text display tests
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);

      // Define inputs and outputs
      pinMode(trigPin, OUTPUT);
      pinMode(echoPin, INPUT);
   }

   void loop() {

      // Clear the trigPin by setting it LOW:
      digitalWrite(trigPin, LOW);

      delayMicroseconds(5);
      // Trigger the sensor by setting the trigPin high for 10 microseconds:
      digitalWrite(trigPin, HIGH);
      delayMicroseconds(10);
      digitalWrite(trigPin, LOW);

      // Read the echoPin. pulseIn() returns the duration (length of the pulse) in microseconds:
      duration = pulseIn(echoPin, HIGH);

      // Calculate the distance:
      distance = duration * 0.034 / 2;

      display.clearDisplay();
      display.setCursor(0, 0);

      display.print(distance, 1);
      display.print(" CM");
      display.println();

      display.display();
   }
};