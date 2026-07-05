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
//re
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
#include "Timer.h"

#include "WiFiSettings.h"

constexpr const char* location = "StudioCloset";
constexpr auto version = "v1.0";
constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15;
constexpr auto SENSOR_INTERVAL_MS = 500;
constexpr auto WATCHDOG_INTERVAL_MS = 60 * 1000;
constexpr auto RESET_DELAY_S = 10;
constexpr auto MAX_CHARS = 8;
constexpr auto SPACING = 8;
constexpr auto SENSOR_POST_FAILURE_SLEEP_S = 60;
constexpr uint8_t TEXT_SIZE_SMALL = 2;
constexpr uint8_t INFLUX_TEMP_DECIMAL_PLACES = 3;
constexpr uint8_t INFLUX_HUMIDITY_DECIMAL_PLACES = 2;

Format humFormat("##.#%");
Format tempFormat("###.## F");

Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);
TempSensor sensor;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Influx influx(WIFI_SSID, WIFI_PASSWORD, &client, &status);
InfluxPoint point(INFLUX_MEASUREMENT);
InfluxField* tempField = point.addTimeAveragedField(INFLUX_INTERVAL_S, "temperature", INFLUX_TEMP_DECIMAL_PLACES);
InfluxField* humField = point.addTimeAveragedField(INFLUX_INTERVAL_S, "humidity", INFLUX_HUMIDITY_DECIMAL_PLACES);
Timer sensorTimer(SENSOR_INTERVAL_MS);
Timer influxTimer(INFLUX_INTERVAL_S * 1000);

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();
   pinMode(BUILTIN_LED, OUTPUT);

   status.begin();

   arduino.echoToSerial = true;
   arduino.clearDisplay();
   arduino.setTextSize(TEXT_SIZE_SMALL);
   arduino.println("Initializing", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 2);

   arduino.print("Loc: ", Color::LABEL);
   arduino.printlnR(location, Color::VALUE);

   arduino.print("Sensor... ", Color::LABEL);
   if (sensor.begin(true))
   {
      arduino.printlnR("ok", Color::VALUE);
      Serial.print("   Type: ");
      Serial.println(sensor.type());
      Serial.print("   Address: ");
      Serial.println(sensor.address());
      Serial.print("   ID: ");
      Serial.println(sensor.id());
   }
   else
   {
      arduino.printR("FAILED", Color::RED);
      Util::reset(RESET_DELAY_S);
   }

   if (!influx.begin(&arduino))
   {
      Util::reset(RESET_DELAY_S);
   }

   delay(1000);

   point.addTag("location", location);

   arduino.clearDisplay();
   arduino.echoToSerial = false;

   Watchdog.enable(WATCHDOG_INTERVAL_MS);
}

void loop()
{
   Watchdog.reset();

   if (sensorTimer.ready())
   {
      tempField->set(sensor.readTemperatureF());
      humField->set(sensor.readHumidity());
   }

   if (!influx.ensureWiFiConnected())
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

   float temp = tempField->get();
   float hum = humField->get();
   uint8_t x = (arduino.width() - MAX_CHARS * arduino.charW()) / 2;

   arduino.setTextSize(4);
   arduino.setCursor(x, (arduino.height() - 2 * arduino.charH()) / 2 - SPACING / 2);
   arduino.println(temp, tempFormat, Color::VALUE);

   arduino.setCursor(x, arduino.getCursorY() + SPACING);
   arduino.println(hum, humFormat, Color::VALUE);

   arduino.setTextSize(TEXT_SIZE_SMALL);
   arduino.setCursor(0, -arduino.charH());
   arduino.print(location, Color::CYAN);
   arduino.printR(version, Color::SUB_LABEL);

   if (influxTimer.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      if (!point.post(&client))
      {
         arduino.deepSleep(SENSOR_POST_FAILURE_SLEEP_S);
      }
      digitalWrite(BUILTIN_LED, LOW);
   }
}
