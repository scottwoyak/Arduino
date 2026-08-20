//
// Displays live temperature and humidity from a single sensor and uploads averaged
// readings to InfluxDB on a fixed interval.
//
// Behavior:
// - Initializes display, sensor, NeoPixel status LED, watchdog, and InfluxDB client.
// - Samples temperature and humidity every SENSOR_INTERVAL_MS and accumulates
//   time-averaged values for the next upload.
// - Continuously renders the latest averaged temperature and humidity values centered
//   on the display at large text size, with location and version in the header/footer.
// - Verifies Wi-Fi connectivity each loop and resets the device if it cannot reconnect.
// - Posts telemetry to InfluxDB every INFLUX_INTERVAL_S seconds.
//
// Failure handling:
// - Sensor initialization failure triggers a device reset after RESET_DELAY_S seconds.
// - Influx initialization failure triggers a device reset after RESET_DELAY_S seconds.
// - Runtime InfluxDB post failure triggers deep sleep for SENSOR_POST_FAILURE_SLEEP_S
//   seconds before the device wakes and retries.
//
// Outputs:
// - Display: centered temperature (###.## F) and humidity (##.#%) at text size 4;
//   location and version shown at small size in the top-left and top-right corners.
// - Serial: sensor type, address, and ID printed during initialization.
//
// Usage:
// - Flash to an Adafruit Feather ESP32-S3 TFT.
// - Power on and allow initialization to complete.
// - Observe live values on the display and periodic telemetry uploads in InfluxDB.
//
#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_LED_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Feather ESP32-S3 or Waveshare ESP32-S3-Zero)."
#endif

#include "TempSensor.h"
#include <Adafruit_SleepyDog.h>
#include "SerialX.h"
#include "Influx.h"
#include "Rebooter.h"
#include "Timer.h"

#include "WiFiSettings.h"

constexpr const char* LOCATION = "Printer";
constexpr auto VERSION = "v1.0";
constexpr auto INFLUX_MEASUREMENT = "Sensors";
constexpr uint8_t INFLUX_INTERVAL_S = 15;
constexpr uint16_t SENSOR_INTERVAL_MS = 500;
constexpr uint8_t WATCHDOG_INTERVAL_S = 60;
constexpr uint8_t RESET_DELAY_S = 10;
constexpr uint8_t SPACING = 8;
constexpr uint8_t SENSOR_POST_FAILURE_SLEEP_S = 60;
constexpr uint8_t TEXT_SIZE_SMALL = 2;
constexpr uint8_t VALUE_TEXT_SIZE = 4;
constexpr uint8_t INFLUX_TEMP_DECIMAL_PLACES = 3;
constexpr uint8_t INFLUX_HUMIDITY_DECIMAL_PLACES = 2;

Format humFormat("##.#%");
Format tempFormat("###.## F");

Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);
TempSensor sensor;
Influx influx(INFLUX_INTERVAL_S, &status);
InfluxPoint point(INFLUX_MEASUREMENT);
InfluxField* tempField = point.addTimeAverageField(INFLUX_INTERVAL_S, "temperature", INFLUX_TEMP_DECIMAL_PLACES);
InfluxField* humField = point.addTimeAverageField(INFLUX_INTERVAL_S, "humidity", INFLUX_HUMIDITY_DECIMAL_PLACES);
Timer sensorTimer(SENSOR_INTERVAL_MS);
Rebooter rebooter;

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();
   pinMode(BUILTIN_LED, OUTPUT);

   status.begin();

   arduino.beginInit();

   arduino.println("Location: ", LOCATION);

   if (arduino.initSensor("Sensor", []() { return sensor.begin(false); }))
   {
      Serial.print("   Type: ");
      Serial.println(sensor.type());
      Serial.print("   Address: ");
      Serial.println(sensor.address());
      Serial.print("   ID: ");
      Serial.println(sensor.id());
   }
   else
   {
      Util::reset(RESET_DELAY_S);
   }

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD);
   if (!influx.begin(arduino))
   {
      Util::reset(RESET_DELAY_S);
   }

   rebooter.begin();

   // Pause so the initialization info on the display remains visible for a moment
   // before it's cleared and replaced with the live temperature/humidity readout.
   arduino.setCursor(0, -arduino.charH());
   arduino.println("Starting in 5s...", Color::GRAY);
   delay(5000);

   point.addTag("location", LOCATION);

   arduino.clearDisplay();

   Watchdog.enable(WATCHDOG_INTERVAL_S * 1000);
}

void loop()
{
   Watchdog.reset();

   rebooter.loop();

   if (sensorTimer.ready())
   {
      tempField->set(sensor.readTemperatureF());
      humField->set(sensor.readHumidity());
   }

   if (!arduino.ensureWiFiConnected(&status))
   {
      arduino.clearDisplay();
      arduino.println("WiFi connection lost", Color::RED);
      Util::reset(RESET_DELAY_S);
   }

   arduino.setCursor(0, 0);
   arduino.setTextSize(TEXT_SIZE_SMALL);
   arduino.print("Influx", Color::HEADING);
   arduino.printR(sensor.type(), Color::GRAY);
   arduino.println();
   int16_t headerHeight = arduino.charH();
   int16_t footerHeight = arduino.charH(TEXT_SIZE_SMALL);

   float temp = tempField->get();
   float hum = humField->get();

   arduino.setTextSize(VALUE_TEXT_SIZE);
   int16_t valuesHeight = 2 * arduino.charH() + SPACING;
   int16_t availableHeight = arduino.height() - headerHeight - footerHeight;
   arduino.setCursorY(headerHeight + (availableHeight - valuesHeight) / 2);
   arduino.printlnD(temp, tempFormat, Color::VALUE);

   arduino.setCursorY(arduino.getCursorY() + SPACING);
   if (sensor.supportsHumidity())
   {
      arduino.printlnD(hum, humFormat, Color::VALUE);
   }
   else
   {
      arduino.printlnD(humFormat, Color::GRAY);
   }

   arduino.setTextSize(TEXT_SIZE_SMALL);
   arduino.setCursor(0, -arduino.charH());
   arduino.print(LOCATION, Color::CYAN);
   arduino.printR(VERSION, Color::SUB_LABEL);

   if (influx.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      if (!point.post(influx.client()))
      {
         arduino.deepSleep(SENSOR_POST_FAILURE_SLEEP_S);
      }
      digitalWrite(BUILTIN_LED, LOW);
   }
}
