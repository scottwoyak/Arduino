//
// Reads live temperature and humidity from a single sensor and uploads averaged
// readings to InfluxDB on a fixed interval. This is the display-free counterpart to
// Temp_Monitor_Display, intended for a Waveshare ESP32-S3-Zero board with no display.
//
// Behavior:
// - Uses the shared MonitorSketch class (see MonitorSketch.h) to own the boot/init sequence: status
//   LED, sensor init hook, WiFi, daily rebooter, OTA, and the standard InfluxDB
//   setup/post/flush cycle.
// - This device's bucket/site/location is prompted for over Serial the first time it
//   runs, then saved to Preferences (NVS) so it survives reboots and OTA firmware
//   updates. On subsequent boots the saved value is used automatically, unless a Serial
//   sketch is attached at boot, which offers a re-prompt. This is handled by the
//   shared MonitorSketch class (see MonitorSketch.h) via SKETCH_CONFIG's promptForContext flag,
//   since this sketch prompts for a bucket/site (chosen from a fixed list) and a
//   free-text location, rather than picking a single fixed SiteConfig entry.
// - Samples temperature and humidity every SENSOR_INTERVAL_MS and accumulates
//   time-averaged values for the next upload.
// - Verifies Wi-Fi connectivity each loop (handled by SketchBase::loop()) and resets
//   the device after SketchBase::WIFI_LOST_RESET_DELAY_S seconds if it cannot reconnect.
// - Posts telemetry to InfluxDB every INFLUX_INTERVAL_S seconds (handled by Monitor::loop()).
// - Checks for a firmware update periodically and, if a newer version is published,
//   downloads and installs it before restarting.
//
// Failure handling:
// - Sensor initialization failure triggers a device reset after config.sensorFailureResetDelayS seconds.
// - Influx initialization failure (handled by Monitor::begin()) triggers a device reset.
// - Runtime InfluxDB post/flush failures are logged to Serial by Monitor::loop() and
//   retried the following cycle.
//
// Outputs:
// - Serial: sensor type printed during initialization; the resolved (or
//   prompted-for) site/location printed after WiFi connects.
//
// Usage:
// - Flash to a Waveshare ESP32-S3-Zero (wired per WaveShare_ESP32_S3_Zero_Sensors).
// - Power on and allow initialization to complete.
// - Observe periodic telemetry uploads in InfluxDB.
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

// This board is wired with an onboard NeoPixel status LED and no display or buttons.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

// Uncomment this to log to the local LogServer instead of the remote one.
//#define LOG_SERVER_LOCAL

#include <string>

#include <esp_pm.h>

#include "ArduinoBoard.h"

#ifdef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch is for boards without a display (e.g. Waveshare ESP32-S3-Zero); use Temp_Monitor_Display instead."
#include "WrongBoard.h"
#endif
#ifndef ARDUINO_NEOPIXEL_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Waveshare ESP32-S3-Zero)."
#include "WrongBoard.h"
#endif

#include "LibraryVersion.h"
#include "SerialX.h"
#include "Timer.h"

#include "WiFiSettings.h"

#include "TempMonitorSketch.h"

// This sketch's own version (e.g. "2.0"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("2.2");
constexpr auto SKETCH_NAME = "Temp_Monitor";
constexpr auto PREFERENCES_NAMESPACE = "TempMonitor";

// Extra status LED wired directly to the board: signal on LED_STATUS_PIN, ground on
// LED_STATUS_GROUND_PIN (held LOW), alongside the board's built-in RGB LED/NeoPixel status.
constexpr uint8_t LED_STATUS_PIN = 13;
constexpr uint8_t LED_STATUS_GROUND_PIN = 12;

// CPU frequency used by TempMonitorSketch (see SketchConfig::cpuFrequencyMhz), passed
// to esp_pm_configure() below so automatic light sleep doesn't fight the sketch's
// fixed clock speed.
constexpr uint8_t CPU_FREQ_MHZ = 80;

// How long loop() yields to the idle task each pass so the ESP32 can drop into
// automatic light sleep between sensor samples (sampled only every few seconds),
// reducing CPU temperature. This sketch has no display/buttons to service, so a
// headless device without a display can afford to sleep aggressively between loop()
// passes; Temp_Monitor_Display does not do this since it needs to stay responsive.
constexpr uint8_t LOOP_SLEEP_MS = 100;

SingleLedStatus ledStatus(LED_STATUS_PIN);

TempMonitorSketch sketch(SKETCH_NAME, VERSION, PREFERENCES_NAMESPACE);

void setup()
{
   // the led ground pin is wired to a pin, so power it up
   pinMode(LED_STATUS_GROUND_PIN, OUTPUT);
   digitalWrite(LED_STATUS_GROUND_PIN, LOW);

   Wire.begin();

   // Registered before sketch.begin() (which calls arduino.begin()) so ledStatus.begin()
   // is invoked along with the board's built-in status indicators.
   sketch.arduino.addStatus(&ledStatus);

   sketch.begin();

   // Allow the idle task to put the CPU into automatic light sleep whenever loop()
   // yields (see the delay() in loop() below). WiFi stays connected via modem sleep,
   // so this does not disrupt WiFi/OTA/Influx timing, but reduces CPU temperature
   // between the infrequent sensor samples/uploads this sketch performs.
   esp_pm_config_t pmConfig{ .max_freq_mhz = CPU_FREQ_MHZ, .min_freq_mhz = CPU_FREQ_MHZ, .light_sleep_enable = true };
   esp_err_t pmResult = esp_pm_configure(&pmConfig);
   if (pmResult != ESP_OK)
   {
      Logger.log("Failed to enable automatic light sleep", LogSeverity::WARN);
   }

   Logger.logInitializationComplete();
}

void loop()
{
   sketch.loop();

   // Yield to the idle task so the CPU can enter automatic light sleep (configured
   // in setup() above) instead of busy-spinning between sensor samples.
   delay(LOOP_SLEEP_MS);
}
