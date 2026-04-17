// 
// Sketch for DS18B20 temperature sensor display on a Feather TFT ESP32 S3.
//

#include <Feather.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Stopwatch.h>

#define ONE_WIRE_PIN 5

Feather feather;
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

// create this define to use a DS18B20 sensor. If not defined, one of the I2C sensors will be auto detected

Format tempFormat("####.## F");
Format timeFormat("####.# ms");
Format rateFormat("####/s");
Format resolutionFormat("##");

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

   feather.begin();

   sensors.begin();
}

void loop()
{
   if (feather.buttonA.wasPressed())
   {
      uint8_t resolution = sensors.getResolution();
      resolution = (resolution == 12) ? 9 : resolution + 1;
      sensors.setResolution(resolution);
   }

   feather.setCursor(0, 0);

   // read sensor
   Stopwatch sw;
   sensors.requestTemperatures();
   float temp = sensors.getTempFByIndex(0);
   float elapsedMs = sw.elapsedMillis();

   // display values
   feather.setTextSize(3);
   feather.print("Temp: ", Color::LABEL);
   feather.println(temp, tempFormat, Color::VALUE);

   feather.print("Resolution: ", Color::LABEL);
   feather.println(sensors.getResolution(), resolutionFormat, Color::VALUE);

   feather.print("Time: ", Color::LABEL);
   feather.println(elapsedMs, timeFormat, Color::VALUE);

   feather.print("Rate: ", Color::LABEL);
   feather.println(1000.0/elapsedMs, rateFormat, Color::VALUE);

   feather.setTextSize(2);
   feather.setCursorY(-2*feather.charH());
   feather.println("Press button A", Color::SUB_LABEL);
   feather.println("to change resolution", Color::SUB_LABEL);
}


