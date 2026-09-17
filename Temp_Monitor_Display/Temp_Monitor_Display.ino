//
// Displays live temperature and humidity from a single sensor and uploads averaged
// readings to InfluxDB on a fixed interval.
//
// Behavior:
// - Uses the shared Monitor class (see Monitor.h) to own the boot/init sequence:
//   display init, status LED, sensor init hook, WiFi, daily rebooter, OTA, and the
//   standard InfluxDB setup/post/flush cycle.
// - This device's bucket/site/location is prompted for over Serial the first time it
//   runs, then saved to Preferences (NVS) so it survives reboots and OTA firmware
//   updates. On subsequent boots the saved value is used automatically, unless buttonA
//   is held during a short window right after startup, which forces a re-prompt. This is
//   handled by the shared Monitor class (see Monitor.h) via MONITOR_CONFIG's
//   promptForContext flag, since this sketch prompts for a bucket (from a fixed list) and
//   free-text site/location, rather than picking a single fixed SiteConfig entry.
// - Samples temperature and humidity every SENSOR_INTERVAL_MS and accumulates
//   time-averaged values for the next upload.
// - Continuously renders the latest averaged temperature and humidity values centered
//   on the display at large text size, with location and version in the header/footer.
// - Verifies Wi-Fi connectivity each loop (handled by SketchBase::loop()); on loss,
//   clears the display and shows "WiFi connection lost" (via setOnWiFiLostCallback())
//   before resetting the device after SketchBase::WIFI_LOST_RESET_DELAY_S seconds.
// - Posts telemetry to InfluxDB every INFLUX_INTERVAL_S seconds (handled by Monitor::loop()).
// - Checks for a firmware update periodically and, if a newer
//   version is published, downloads and installs it (showing progress on the display)
//   before restarting.
//
// Failure handling:
// - Sensor initialization failure triggers a device reset after config.influx.sensorFailureResetDelayS seconds.
// - Influx initialization failure (handled by Monitor::begin()) triggers a device reset.
// - Runtime InfluxDB post/flush failures are logged to Serial by Monitor::loop() and
//   retried the following cycle.
//
// Outputs:
// - Display: centered temperature (###.## F) and humidity (##.#%) at text size 4;
//   bucket/site/location and version shown at small size in the top-left and top-right corners.
// - Serial: sensor type, address, and ID printed during initialization; the resolved (or
//   prompted-for) site/location printed after WiFi connects.
//
// Usage:
// - Flash to an Adafruit Feather ESP32-S3 TFT.
// - Power on and allow initialization to complete.
// - Observe live values on the display and periodic telemetry uploads in InfluxDB.
//
// InfluxDB points uploaded (Measurement: Sensors):
//
// - site=<from Serial prompt/Preferences>, location=<from Serial prompt/Preferences>, sensor=Temperature
//     temperature: time-averaged value of sensor.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, averaged over the INFLUX_INTERVAL_S upload interval.
//     humidity: time-averaged value of sensor.readHumidity(), sampled every
//     SENSOR_INTERVAL_MS, averaged over the INFLUX_INTERVAL_S upload interval.
//     dewPoint, absoluteHumidity, heatIndex: time-averaged values derived from the
//     same temperature/humidity reading (see TempSensor::readAll()), sampled every
//     SENSOR_INTERVAL_MS, averaged over the INFLUX_INTERVAL_S upload interval.
//
#include <string>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_NEOPIXEL_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Feather ESP32-S3 or Waveshare ESP32-S3-Zero)."
#endif

#include "TempSensor.h"
#include "SerialX.h"
#include "Timer.h"
#include "FieldTable.h"

#include "WiFiSettings.h"

#include "Monitor.h"

// version.txt contains a quoted version string (e.g. "v1.1") and is included directly here
// so the compiled-in VERSION always matches the same file uploaded to the GitHub release,
// with no separate sync step required.
constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Temp_Monitor_Display";
constexpr auto INFLUX_SENSOR = "Temperature";
constexpr auto PREFERENCES_NAMESPACE = "TempMonitor";
constexpr uint8_t INFLUX_INTERVAL_S = 15;
constexpr uint16_t SENSOR_INTERVAL_MS = 500;
constexpr uint8_t SPACING = 8;
constexpr uint8_t TEXT_SIZE_SMALL = 2;
constexpr uint8_t VALUE_TEXT_SIZE = 4;
constexpr uint8_t INFLUX_TEMP_DECIMAL_PLACES = 3;
constexpr uint8_t INFLUX_HUMIDITY_DECIMAL_PLACES = 2;
constexpr uint8_t STARTUP_DELAY_S = 5;

Format humFormat("##.#%");
Format tempFormat("###.## F");

Arduino arduino;
TempSensor sensor;
Timer sensorTimer(SENSOR_INTERVAL_MS);

InfluxField* tempField = nullptr;
InfluxField* humField = nullptr;
InfluxField* dewPointField = nullptr;
InfluxField* absoluteHumidityField = nullptr;
InfluxField* heatIndexField = nullptr;

// Cached values
// FieldTable reads these directly via pointer and only repaints a row's value when it changes.
float allValuesTemp = NAN;

// Backs the all-values table's humidity, dew point, absolute humidity, and heat index rows:
// when there is no humidity sensor, these are set to a dash placeholder (rather than a NaN
// float, which now renders as literal "nan" text) since none of these values can be computed
// without a humidity reading.
StringValue allValuesHum("##.#%");
StringValue allValuesDewPoint("###.## F");
StringValue allValuesAbsHum("##.#%");
StringValue allValuesHeatIndex("###.## F");

// Shows all 5 time-averaged readings with labels, GAP-aligned into a single value
// column, while buttonA is held (see loop()). Positioned once headerHeight is known in
// setup().
FieldTable allValuesTable(&arduino, 0, 0, TEXT_SIZE_SMALL);

// Tracks whether the buttonA-held all-values table was showing on the previous loop() so
// the display can be cleared exactly once when switching between it and the normal readout.
bool wasAllValuesMode = false;

InfluxContext INFLUX_CONTEXT = {
   .sensor = INFLUX_SENSOR,
};

InfluxConfig INFLUX_CONFIG = {
   .context = INFLUX_CONTEXT,
   .intervalS = INFLUX_INTERVAL_S,
   .promptForContext = true,
};

SketchConfig MONITOR_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .influx = INFLUX_CONFIG,
   .enableOTA = true,
   .enableRebooter = true,
};

Monitor monitor(&arduino, MONITOR_CONFIG);

void setup()
{
   Wire.begin();

   monitor.setOnWiFiLostCallback([]()
   {
      arduino.clearDisplay();
      arduino.println("WiFi connection lost", Color::RED);
      return false;
   });

   monitor.begin();

   // Fall back to the internal ESP32 CPU temperature sensor if no external sensor is
   // found, so the device still reports a (less accurate) temperature reading instead
   // of failing to start.
   if (arduino.initSensor("Sensor", []() { return sensor.begin(false, true); }, []() { return sensor.type(); }))
   {
      std::string addressStr = sensor.address() != 0 ? std::string("0x") + String(sensor.address(), HEX).c_str() : "N/A";
      std::string idStr = strlen(sensor.id()) > 0 ? sensor.id() : "N/A";
      std::string sensorMessage = std::string("Sensor: ") + sensor.type() + ", Address: " + addressStr + ", ID: " + idStr;
      monitor.logMessage(sensorMessage.c_str());
   }
   else
   {
      monitor.reportSensorFailure();
   }

   InfluxPoint* point = monitor.addPoint();
   tempField = point->addTimeAverageField(INFLUX_INTERVAL_S, "temperature", INFLUX_TEMP_DECIMAL_PLACES);
   humField = point->addTimeAverageField(INFLUX_INTERVAL_S, "humidity", INFLUX_HUMIDITY_DECIMAL_PLACES);
   dewPointField = point->addTimeAverageField(INFLUX_INTERVAL_S, "dewPoint", INFLUX_TEMP_DECIMAL_PLACES);
   absoluteHumidityField = point->addTimeAverageField(INFLUX_INTERVAL_S, "absoluteHumidity", INFLUX_HUMIDITY_DECIMAL_PLACES);
   heatIndexField = point->addTimeAverageField(INFLUX_INTERVAL_S, "heatIndex", INFLUX_TEMP_DECIMAL_PLACES);

   allValuesTable.addRow("Temp", tempFormat.formatString().c_str(), &allValuesTemp);
   allValuesTable.addRow("Humidity", &allValuesHum);
   allValuesTable.addRow("Dew Pt", &allValuesDewPoint);
   allValuesTable.addRow("Abs Hum", &allValuesAbsHum);
   allValuesTable.addRow("Heat Idx", &allValuesHeatIndex);

   int16_t tableHeaderHeight = arduino.charH(TEXT_SIZE_SMALL);
   int16_t tableFooterHeight = arduino.charH(TEXT_SIZE_SMALL);
   int16_t tableAvailableHeight = arduino.height() - tableHeaderHeight - tableFooterHeight;
   allValuesTable.setPosition(arduino.width() / 2, tableHeaderHeight + tableAvailableHeight / 2, Anchor::CENTER);

   // Pause so the initialization info on the display remains visible for a moment
   // before it's cleared and replaced with the live temperature/humidity readout.
   delay(STARTUP_DELAY_S * 1000UL);

   arduino.clearDisplay();
}

void loop()
{
   monitor.loop();

   if (sensorTimer.ready())
   {
      Readings readings = sensor.readAll();
      tempField->set(readings.tempF);
      humField->set(readings.humidity);
      dewPointField->set(readings.dewPointF);
      absoluteHumidityField->set(readings.absoluteHumidity);
      heatIndexField->set(readings.heatIndexF);
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

   bool allValuesMode = arduino.buttonA.isPressed();
   if (allValuesMode != wasAllValuesMode)
   {
      // Switching between the all-values table and the normal readout: clear only the
      // content area between the header and footer, to avoid flicker from clearing the
      // header/footer content that isn't changing.
      arduino.clear(Rect16{ 0, (uint16_t)headerHeight, arduino.width(), (uint16_t)(arduino.height() - headerHeight - footerHeight) });
      allValuesTable.invalidate();
      wasAllValuesMode = allValuesMode;
   }

   if (allValuesMode)
   {
      // Show all 5 time-averaged readings with labels, column-aligned via FieldTable,
      // while buttonA is held.
      allValuesTemp = temp;
      if (sensor.supportsHumidity())
      {
         allValuesHum.set(humFormat.toString(hum));
         allValuesDewPoint.set(tempFormat.toString(dewPointField->get()));
         allValuesAbsHum.set(humFormat.toString(absoluteHumidityField->get()));
         allValuesHeatIndex.set(tempFormat.toString(heatIndexField->get()));
      }
      else
      {
         allValuesHum.set(humFormat.toNoValueString());
         allValuesDewPoint.set(tempFormat.toNoValueString());
         allValuesAbsHum.set(humFormat.toNoValueString());
         allValuesHeatIndex.set(tempFormat.toNoValueString());
      }
      allValuesTable.draw();
   }
   else
   {
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
   }

   arduino.setTextSize(TEXT_SIZE_SMALL);
   arduino.setCursor(0, -arduino.charH());
   arduino.print(monitor.bucketSiteLocation(), Color::CYAN);
   arduino.printR(VERSION, Color::SUB_LABEL);
}
