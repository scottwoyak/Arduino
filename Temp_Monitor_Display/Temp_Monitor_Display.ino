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
#include "Feather_ESP32_S3.h"
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

Feather_ESP32_S3 feather;
NeoPixelStatus status(&feather.neoPixel);
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

   feather.begin();
   pinMode(BUILTIN_LED, OUTPUT);

   status.begin();

   feather.echoToSerial = true;
   feather.clearDisplay();
   feather.setTextSize(TEXT_SIZE_SMALL);
   feather.println("Initializing", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.print("Loc: ", Color::LABEL);
   feather.printlnR(location, Color::VALUE);

   feather.print("Sensor... ", Color::LABEL);
   if (sensor.begin(true))
   {
      feather.printlnR("ok", Color::VALUE);
      Serial.print("   Type: ");
      Serial.println(sensor.type());
      Serial.print("   Address: ");
      Serial.println(sensor.address());
      Serial.print("   ID: ");
      Serial.println(sensor.id());
   }
   else
   {
      feather.printR("FAILED", Color::RED);
      Util::reset(RESET_DELAY_S);
   }

   if (!influx.begin(&feather))
   {
      Util::reset(RESET_DELAY_S);
   }

   delay(1000);

   point.addTag("location", location);

   feather.clearDisplay();
   feather.echoToSerial = false;

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
      feather.clearDisplay();
      feather.println("WiFi connection lost", Color::RED);
      Util::reset(RESET_DELAY_S);
   }

   feather.setCursor(0, 0);
   feather.setTextSize(TEXT_SIZE_SMALL);
   feather.print("Influx", Color::HEADING);
   feather.printR(sensor.type(), Color::GRAY);
   feather.println();

   float temp = tempField->get();
   float hum = humField->get();
   uint8_t x = (feather.width() - MAX_CHARS * feather.charW()) / 2;

   feather.setTextSize(4);
   feather.setCursor(x, (feather.height() - 2 * feather.charH()) / 2 - SPACING / 2);
   feather.println(temp, tempFormat, Color::VALUE);

   feather.setCursor(x, feather.getCursorY() + SPACING);
   feather.println(hum, humFormat, Color::VALUE);

   feather.setTextSize(TEXT_SIZE_SMALL);
   feather.setCursor(0, -feather.charH());
   feather.print(location, Color::CYAN);
   feather.printR(version, Color::SUB_LABEL);

   if (influxTimer.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      if (!point.post(&client))
      {
         feather.deepSleep(SENSOR_POST_FAILURE_SLEEP_S);
      }
      digitalWrite(BUILTIN_LED, LOW);
   }
}
