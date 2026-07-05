// 
// Sketch for DS18B20 temperature sensor display on a Feather TFT ESP32 S3.
//

#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Rate.h"

#define ONE_WIRE_PIN 5

Arduino arduino;
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

// create this define to use a DS18B20 sensor. If not defined, one of the I2C sensors will be auto detected

Format tempFormat("####.## F");
Format timeFormat("####.# ms");
Format rateFormat("####/s");
Format resolutionFormat("##");

Rate rate;

// The setup() function runs once each time the micro-controller starts
void setup()
{
   Wire.begin();

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

   arduino.begin();

   sensors.begin();
}

void loop()
{
   if (arduino.buttonA.wasPressed())
   {
      uint8_t resolution = sensors.getResolution();
      resolution = (resolution == 12) ? 9 : resolution + 1;
      sensors.setResolution(resolution);
   }

   arduino.setCursor(0, 0);

   // read sensor
   rate.start();
   sensors.requestTemperatures();
   float temp = sensors.getTempFByIndex(0);
   rate.stop();
   float elapsedMs = rate.elapsedMicros() / 1000.0f;

   // display values
   arduino.setTextSize(3);
   arduino.println("Temp: ", temp, tempFormat);
   arduino.println("Resolution: ", sensors.getResolution(), resolutionFormat);
   arduino.println("Time: ", elapsedMs, timeFormat);
   arduino.println("Rate: ", rate.get(), rateFormat);

   arduino.setTextSize(2);
   arduino.setCursorY(-2*arduino.charH());
   arduino.println("Press button A", Color::SUB_LABEL);
   arduino.println("to change resolution", Color::SUB_LABEL);
}


