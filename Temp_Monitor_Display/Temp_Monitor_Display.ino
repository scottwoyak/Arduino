//
// Displays live temperature and humidity from a single sensor and uploads averaged
// readings to InfluxDB on a fixed interval.
//
// Behavior:
// - Initializes display, sensor, NeoPixel status LED, watchdog, and InfluxDB client.
// - After WiFi connects, fetches this device's location/site from a shared JSON config
//   file (see DeviceConfig.h), keyed by this device's WiFi MAC address.
// - Samples temperature and humidity every SENSOR_INTERVAL_MS and accumulates
//   time-averaged values for the next upload.
// - Continuously renders the latest averaged temperature and humidity values centered
//   on the display at large text size, with location and version in the header/footer.
// - Verifies Wi-Fi connectivity each loop and resets the device if it cannot reconnect.
// - Posts telemetry to InfluxDB every INFLUX_INTERVAL_S seconds.
// - Checks for a firmware update every OTA_CHECK_INTERVAL_M minutes and, if a newer
//   version is published, downloads and installs it (showing progress on the display)
//   before restarting.
//
// Failure handling:
// - Sensor initialization failure triggers a device reset after RESET_DELAY_S seconds.
// - Device config fetch/lookup retries indefinitely until it succeeds (this device's
//   MAC address must be present in the shared config).
// - Influx initialization failure triggers a device reset after RESET_DELAY_S seconds.
// - Runtime InfluxDB post failure triggers deep sleep for SENSOR_POST_FAILURE_SLEEP_S
//   seconds before the device wakes and retries.
//
// Outputs:
// - Display: centered temperature (###.## F) and humidity (##.#%) at text size 4;
//   location and version shown at small size in the top-left and top-right corners.
// - Serial: sensor type, address, and ID printed during initialization; MAC address and
//   the fetched location/site printed after WiFi connects.
//
// Usage:
// - Flash to an Adafruit Feather ESP32-S3 TFT.
// - Power on and allow initialization to complete.
// - Observe live values on the display and periodic telemetry uploads in InfluxDB.
//
// InfluxDB points uploaded (Measurement: Sensors):
//
// - site=<from DeviceConfig>, location=<from DeviceConfig>, sensor=Temperature
//     temperature: time-averaged value of sensor.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, averaged over the INFLUX_INTERVAL_S upload interval.
//     humidity: time-averaged value of sensor.readHumidity(), sampled every
//     SENSOR_INTERVAL_MS, averaged over the INFLUX_INTERVAL_S upload interval.
//
#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_NEOPIXEL_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Feather ESP32-S3 or Waveshare ESP32-S3-Zero)."
#endif

#include <Adafruit_SleepyDog.h>

#include "TempSensor.h"
#include "SerialX.h"
#include "Influx.h"
#include "Rebooter.h"
#include "Timer.h"
#include "DeviceConfig.h"
#include "OTAUpdater.h"

#include "WiFiSettings.h"

constexpr auto DEVICE_CONFIG_URL = "https://raw.githubusercontent.com/scottwoyak/Arduino/main/TempMonitor.json";

// version.txt contains a quoted version string (e.g. "v1.1") and is included directly here
// so the compiled-in VERSION always matches the same file uploaded to the GitHub release,
// with no separate sync step required.
constexpr auto VERSION =
#include "version.txt"
;
constexpr auto OTA_VERSION_URL = "https://github.com/scottwoyak/Arduino/releases/download/Temp-Monitor-Display/version.txt";
constexpr auto OTA_FIRMWARE_URL = "https://github.com/scottwoyak/Arduino/releases/download/Temp-Monitor-Display/Temp_Monitor_Display.ino.bin";
constexpr uint8_t OTA_CHECK_INTERVAL_M = 10;
constexpr auto INFLUX_MEASUREMENT = "Sensors";
constexpr auto INFLUX_SENSOR = "Temperature";
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
constexpr uint8_t STARTUP_DELAY_S = 5;

Format humFormat("##.#%");
Format tempFormat("###.## F");

Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);
TempSensor sensor;
Influx influx(INFLUX_INTERVAL_S, &status);
DeviceConfig deviceConfig;
InfluxPoint* point = nullptr;
InfluxField* tempField = nullptr;
InfluxField* humField = nullptr;
Timer sensorTimer(SENSOR_INTERVAL_MS);
Rebooter rebooter;
OTAUpdater ota(VERSION, OTA_VERSION_URL, OTA_FIRMWARE_URL, &arduino, OTA_CHECK_INTERVAL_M * 60.0f);

///
/// <summary>
/// Formats this device's site and location (as fetched by DeviceConfig) as "Site/Location".
/// </summary>
/// <returns>The formatted "Site/Location" string.</returns>
///
std::string siteLocation()
{
   return std::string(deviceConfig.get("site")) + "/" + deviceConfig.get("location");
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();

   status.begin();
   status.setStatus(Status::STARTED);

   arduino.beginInit();

   arduino.print("Sensor...", Color::LABEL);
   if (sensor.begin(false))
   {
      arduino.printlnR(sensor.type(), Color::VALUE);
      Serial.print("Sensor...");
      Serial.println(sensor.type());
      Serial.print("   Address: 0x");
      Serial.println(sensor.address(), HEX);
      Serial.print("   ID: ");
      Serial.println(sensor.id());
   }
   else
   {
      status.setStatus(Status::FAILED);
      Util::reset(RESET_DELAY_S);
   }

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &status);
   Serial.print("MAC Address: ");
   Serial.println(WiFi.macAddress());

   deviceConfig.begin(DEVICE_CONFIG_URL, { "site", "location" }, &arduino);
   arduino.print("Location...", Color::LABEL);
   arduino.printlnR(siteLocation(), Color::VALUE);

   point = new InfluxPoint(INFLUX_MEASUREMENT, { { "site", deviceConfig.get("site") }, { "location", deviceConfig.get("location") }, { "sensor", INFLUX_SENSOR } });
   tempField = point->addTimeAverageField(INFLUX_INTERVAL_S, "temperature", INFLUX_TEMP_DECIMAL_PLACES);
   humField = point->addTimeAverageField(INFLUX_INTERVAL_S, "humidity", INFLUX_HUMIDITY_DECIMAL_PLACES);

   if (!influx.begin(arduino))
   {
      status.setStatus(Status::FAILED);
      Util::reset(RESET_DELAY_S);
   }

   status.setStatus(Status::READY);

   rebooter.begin();

   // Pause so the initialization info on the display remains visible for a moment
   // before it's cleared and replaced with the live temperature/humidity readout.
   arduino.setCursor(0, -arduino.charH());
   arduino.println("Starting in 5s...", Color::GRAY);
   delay(STARTUP_DELAY_S * 1000UL);

   arduino.clearDisplay();

   Watchdog.enable(WATCHDOG_INTERVAL_S * 1000);
}

void loop()
{
   Watchdog.reset();

   rebooter.loop();
   ota.loop();

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
   arduino.print(siteLocation(), Color::CYAN);
   arduino.printR(VERSION, Color::SUB_LABEL);

   if (influx.ready())
   {
      arduino.led.turnOn();
      if (!point->post(influx.client()))
      {
         arduino.deepSleep(SENSOR_POST_FAILURE_SLEEP_S);
      }
      arduino.led.turnOff();
   }
}
