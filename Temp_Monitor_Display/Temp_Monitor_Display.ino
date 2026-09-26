//
// Displays live temperature and humidity from a single sensor and uploads averaged
// readings to InfluxDB on a fixed interval.
//
// Behavior:
// - Uses the shared MonitorSketch class (see MonitorSketch.h) to own the boot/init sequence:
//   display init, status LED, sensor init hook, WiFi, daily rebooter, OTA, and the
//   standard InfluxDB setup/post/flush cycle.
// - This device's bucket/site/location is prompted forfirst time it
//   runs, then saved to Preferences (NVS) so it survives reboots and OTA firmware
//   updates. On subsequent boots the saved value is used automatically, over Serial the  unless buttonA
//   is held during a short window right after startup, which forces a re-prompt. This is
//   handled by the shared MonitorSketch class (see MonitorSketch.h) via MONITOR_CONFIG's
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
// - Sensor initialization failure triggers a device reset after config.sensorFailureResetDelayS seconds.
// - Influx initialization failure (handled by Monitor::begin()) triggers a device reset.
// - Runtime InfluxDB post/flush failures are logged to Serial by Monitor::loop() and
//   retried the following cycle.
//
// Outputs:
// - Display: centered temperature (###.## F) and humidity (##.#%) at text size 4;
//   site/location and version shown at small size in the bottom-left and bottom-right corners.
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
// - site=<from Serial prompt/Preferences>, location=<from Serial prompt/Preferences>, item=Sensor
//     temperature: time-averaged value of sensor.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, averaged over the INFLUX_INTERVAL_S upload interval.
//     humidity: time-averaged value of sensor.readHumidity(), sampled every
//     SENSOR_INTERVAL_MS, averaged over the INFLUX_INTERVAL_S upload interval.
//     dewPoint, absoluteHumidity: time-averaged values derived from the
//     same temperature/humidity reading (see TempSensor::readAll()), sampled every
//     SENSOR_INTERVAL_MS, averaged over the INFLUX_INTERVAL_S upload interval.
//
// - site=<from Serial prompt/Preferences>, location=<from Serial prompt/Preferences>, item=CPU
//     temperature: the ESP32 CPU temperature at upload time.
//
#include <string>

// Declares which VLW font sizes this sketch actually uses (TEXT_SIZE_SMALL=2 and
// VALUE_TEXT_SIZE=4 below), so ArduinoWithDisplay.h/Fonts/Roboto*.h only compile in
// the needed font data instead of all 7 sizes, reducing flash usage.
#define TEXT_SIZES_CUSTOM
#define TEXT_SIZE_2
#define TEXT_SIZE_4

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_NEOPIXEL_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Feather ESP32-S3 or Waveshare ESP32-S3-Zero)."
#endif

#include "SerialX.h"
#include "Timer.h"
#include "FieldTable.h"
#include "LibraryVersion.h"

#include "WiFiSettings.h"

#include "TempMonitorSketch.h"

// This sketch's own version (e.g. "2.4"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("2.5");
constexpr auto SKETCH_NAME = "Temp_Monitor_Display";
constexpr auto PREFERENCES_NAMESPACE = "TempMonitor";
constexpr uint8_t SPACING = 8;
constexpr uint8_t TEXT_SIZE_SMALL = 2;
constexpr uint8_t VALUE_TEXT_SIZE = 4;
constexpr uint8_t STARTUP_DELAY_S = 5;

Format humFormat("##.#%");
Format tempFormat("###.## F");

Arduino arduino;

// Cached values
// FieldTable reads these directly via pointer and only repaints a row's value when it changes.
float allValuesTemp = NAN;

// Backs the all-values table's humidity, dew point, and absolute humidity rows: when there
// is no humidity sensor, these are set to a dash placeholder (rather than a NaN float,
// which now renders as literal "nan" text) since none of these values can be computed
// without a humidity reading.
StringValue allValuesHum("##.#%");
StringValue allValuesDewPoint("###.## F");
StringValue allValuesAbsHum("##.#%");

// Shows all 5 time-averaged readings with labels, GAP-aligned into a single value
// column, while buttonA is held (see loop()). Positioned once headerHeight is known in
// setup().
FieldTable allValuesTable(&arduino, 0, 0, TEXT_SIZE_SMALL);

// Tracks whether the buttonA-held all-values table was showing on the previous loop() so
// the display can be cleared exactly once when switching between it and the normal readout.
bool wasAllValuesMode = false;

TempMonitorSketch monitor(&arduino, SKETCH_NAME, VERSION, PREFERENCES_NAMESPACE);

void setup()
{
   Wire.begin();

   monitor.setOnWiFiLostCallback([]()
   {
      arduino.clearDisplay();
      arduino.println("WiFi connection lost", Color::RED);
      return false;
   });

   // Fall back to the internal ESP32 CPU temperature sensor if no external sensor is
   // found, so the device still reports a (less accurate) temperature reading instead
   // of failing to start.
   monitor.begin();

   allValuesTable.addRow("Temp", tempFormat.formatString().c_str(), &allValuesTemp);
   allValuesTable.addRow("Humidity", &allValuesHum);
   allValuesTable.addRow("Dew Pt", &allValuesDewPoint);
   allValuesTable.addRow("Abs Hum", &allValuesAbsHum);

   int16_t tableHeaderHeight = arduino.charH(TEXT_SIZE_SMALL);
   int16_t tableFooterHeight = arduino.charH(TEXT_SIZE_SMALL);
   int16_t tableAvailableHeight = arduino.height() - tableHeaderHeight - tableFooterHeight;
   allValuesTable.setPosition(arduino.width() / 2, tableHeaderHeight + tableAvailableHeight / 2, Anchor::CENTER);

   // Pause so the initialization info on the display remains visible for a moment
   // before it's cleared and replaced with the live temperature/humidity readout.
   delay(STARTUP_DELAY_S * 1000UL);

   arduino.clearDisplay();

   Logger.logInitializationComplete();
}

///
/// <summary>
/// Draws the header line ("Influx" + sensor type) at the top of the display.
/// </summary>
/// <returns>The header's height in pixels.</returns>
///
int16_t drawHeader()
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(TEXT_SIZE_SMALL);
   arduino.print("Influx", Color::HEADING);
   arduino.printR(monitor.sensor().type(), Color::GRAY);
   arduino.println();
   return arduino.charH();
}

///
/// <summary>
/// Draws the footer line (site/location + version) at the bottom of the display.
/// </summary>
///
void drawFooter()
{
   arduino.setTextSize(TEXT_SIZE_SMALL);
   arduino.setCursor(0, -arduino.charH());
   arduino.print(monitor.siteLocation(), Color::CYAN);
   arduino.printR(VERSION, Color::SUB_LABEL);
}

///
/// <summary>
/// Clears the content area between the header and footer exactly once when switching
/// between the all-values table and the normal readout, to avoid flicker from clearing
/// the header/footer content that isn't changing.
/// </summary>
/// <param name="allValuesMode">True if the all-values table is currently shown.</param>
/// <param name="headerHeight">The header's height in pixels.</param>
/// <param name="footerHeight">The footer's height in pixels.</param>
///
void updateAllValuesMode(bool allValuesMode, int16_t headerHeight, int16_t footerHeight)
{
   if (allValuesMode != wasAllValuesMode)
   {
      arduino.clear(Rect16{ 0, (uint16_t)headerHeight, arduino.width(), (uint16_t)(arduino.height() - headerHeight - footerHeight) });
      allValuesTable.invalidate();
      wasAllValuesMode = allValuesMode;
   }
}

///
/// <summary>
/// Shows all 5 time-averaged readings with labels, column-aligned via FieldTable,
/// while buttonA is held.
/// </summary>
/// <param name="temp">Current time-averaged temperature.</param>
/// <param name="hum">Current time-averaged humidity.</param>
///
void drawAllValuesReadout(float temp, float hum)
{
   allValuesTemp = temp;
   if (monitor.sensor().supportsHumidity())
   {
      allValuesHum.set(humFormat.toString(hum));
      allValuesDewPoint.set(tempFormat.toString(monitor.dewPoint()));
      allValuesAbsHum.set(humFormat.toString(monitor.absoluteHumidity()));
   }
   else
   {
      allValuesHum.set(humFormat.toNoValueString());
      allValuesDewPoint.set(tempFormat.toNoValueString());
      allValuesAbsHum.set(humFormat.toNoValueString());
   }
   allValuesTable.draw();
}

///
/// <summary>
/// Shows the centered temperature and humidity readout at large text size.
/// </summary>
/// <param name="temp">Current time-averaged temperature.</param>
/// <param name="hum">Current time-averaged humidity.</param>
/// <param name="headerHeight">The header's height in pixels.</param>
/// <param name="footerHeight">The footer's height in pixels.</param>
///
void drawNormalReadout(float temp, float hum, int16_t headerHeight, int16_t footerHeight)
{
   arduino.setTextSize(VALUE_TEXT_SIZE);
   int16_t valuesHeight = 2 * arduino.charH() + SPACING;
   int16_t availableHeight = arduino.height() - headerHeight - footerHeight;
   arduino.setCursorY(headerHeight + (availableHeight - valuesHeight) / 2);
   arduino.printlnD(temp, tempFormat, Color::VALUE);

   arduino.setCursorY(arduino.getCursorY() + SPACING);
   if (monitor.sensor().supportsHumidity())
   {
      arduino.printlnD(hum, humFormat, Color::VALUE);
   }
   else
   {
      arduino.printlnD(humFormat, Color::GRAY);
   }
}

void loop()
{
   monitor.loop();

   int16_t headerHeight = drawHeader();
   int16_t footerHeight = arduino.charH(TEXT_SIZE_SMALL);

   float temp = monitor.temp();
   float hum = monitor.humidity();

   bool allValuesMode = arduino.buttonA.isPressed();
   updateAllValuesMode(allValuesMode, headerHeight, footerHeight);

   if (allValuesMode)
   {
      drawAllValuesReadout(temp, hum);
   }
   else
   {
      drawNormalReadout(temp, hum, headerHeight, footerHeight);
   }

   drawFooter();
}
