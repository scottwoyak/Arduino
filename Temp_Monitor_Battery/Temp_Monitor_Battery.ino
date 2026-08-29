#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "TempSensor.h"
#include <Adafruit_MAX1704x.h>
#include "SerialX.h"
#include "Influx.h"

#include "WiFiSettings.h"

Arduino arduino;
TempSensor sensor;
Adafruit_MAX17048 battery;

constexpr auto INFLUX_INTERVAL_S = 60;
constexpr auto WATCHDOG_TIMEOUT_MS = 60 * 1000;
Influx influx;
InfluxPoint airPoint("Air"); // Influx data point
InfluxPoint powerPoint("Power"); // Influx data point
InfluxField* tempField = airPoint.addValueField("temperature", 2);
InfluxField* humField = airPoint.addValueField("humidity", 2);
InfluxField* voltsField = powerPoint.addValueField("volts", 2);

void goToSleep()
{
   Serial.println("Going to sleep for 60 seconds...");
   Serial.println();
   Serial.println();
   delay(100); // let serial finish

   // sleep for 60 seconds (reboot upon wake up)
   arduino.deepSleep(INFLUX_INTERVAL_S);
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();
   arduino.beginInit();
   pinMode(BUILTIN_LED, OUTPUT);
   digitalWrite(BUILTIN_LED, HIGH);

   // turn off power stuff
   pinMode(NEOPIXEL_POWER, OUTPUT);
   digitalWrite(NEOPIXEL_POWER, LOW);
   pinMode(TFT_BACKLITE, OUTPUT);
   digitalWrite(TFT_BACKLITE, LOW);

   arduino.print("Sensor... ");
   if (sensor.begin(true))
   {
      Serial.println("ok");
   }
   else
   {
      Serial.print("FAILED");
      goToSleep();
   }

   arduino.print("Battery... ");
   if (battery.begin())
   {
      Serial.println("ok");
   }
   else
   {
      Serial.print("FAILED");
      goToSleep();
   }

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD);
   if (!influx.begin(&arduino))
   {
      goToSleep();
   }

   airPoint.addTag("location", "Test");
   powerPoint.addTag("location", "Test");

   arduino.enableWatchdog(WATCHDOG_TIMEOUT_MS);
}

// Add the main program code into the continuous loop() function
void loop()
{
   arduino.loop();

   // Store measured value into point
   tempField->set(sensor.readTemperatureF());
   humField->set(sensor.readHumidity());
   voltsField->set(battery.cellVoltage());

   if (!arduino.ensureWiFiConnected())
   {
      goToSleep();
   }

   // Write points
   Serial.println("Writing data points...");
   if (!airPoint.post(influx.client(), true))
   {
      Serial.println("InfluxDB write failed: ");
      Serial.println(influx.client()->getLastErrorMessage());
   }

   if (powerPoint.post(influx.client(), true) == false)
   {
      Serial.println("InfluxDB write failed: ");
      Serial.println(influx.client()->getLastErrorMessage());
   }

   goToSleep();
}


