//
// Wave Publisher
//
// Reads water depth from an ultrasonic (or MS5837 pressure) sensor and publishes live
// wave height readings over a WebSocket telemetry connection.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   streams live raw depth readings as they're read; the subscribing client is
//   responsible for computing wave height from a running average of these readings.
// - Prints distance and wave height (relative to this device's own running average,
//   for local LED/Serial feedback only) readings to Serial every publish cycle.
// - Drives the general-purpose LED at full brightness while starting up, then switches
//   to a brightness proportional to wave height once telemetry is connected: off at
//   or below LED_WAVE_HEIGHT_LOW_CM, full at or above LED_WAVE_HEIGHT_HIGH_CM, and
//   linearly interpolated in between.
// - Posts the average depth, enclosure temperature/humidity, and CPU temperature to
//   InfluxDB every INFLUX_INTERVAL_S seconds, printing the same values to Serial at
//   that time.
// - Restarts the device every 24 hours to play it safe, and on telemetry disconnect
//   or error.
// - Checks for a firmware update periodically.
//
// Telemetry topic / InfluxDB site selection:
// - The telemetry topic (and matching InfluxDB site/location/bucket) is one of the 2
//   entries in WAVE_SITES: the Lake site (bucket "Sensors") and a Test entry (topic
//   "Waves/Test", site/location "Test", bucket "Testing") for testing without touching
//   production data. The choice is made once and saved to Preferences (NVS) so it
//   survives OTA firmware updates.
// - On startup, if no selection has been saved yet, or if buttonA (the onboard BOOT
//   button) is pressed within a short window after the sketch starts, the user is
//   prompted over Serial to pick an entry from the WAVE_SITES list; the choice is then
//   saved to Preferences.
//
// InfluxDB points uploaded (Measurement: Sensors, bucket=<selected>):
//
// - site=<selected>, location=<selected>, sensor=WaveHeight
//     avgDepth: rolling average of raw depth readings (depth->getDepth()) over the
//     5 minute averaging window (DepthSensorBase::DEFAULT_AVERAGE_DURATION_M); only
//     posted once that window is fully populated (depth->isAverageFull()).
//
// - site=<selected>, location=<selected>, sensor=WaveHeight, item=Enclosure
//     temperature: rolling average of enclosureTemp.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//     humidity: rolling average of enclosureTemp.readHumidity(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//
// - site=<selected>, location=<selected>, sensor=WaveHeight, item=CPU
//     temperature: rolling average of cpuTemp.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

#include <iterator>

constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Wave_Publisher";

// ----------- Telemetry topic / InfluxDB site selection
struct WaveSite
{
   const char* telemetryTopic;
   const char* influxBucket;
   const char* influxSite;
   const char* influxLocation;
};

constexpr WaveSite WAVE_SITES[] = {
   { "Waves/LakeP", "Sensors", "Lake", "Dock" },
   { "Waves/Test", "Testing", "Test", "Test" },
};

constexpr auto PREFERENCES_NAMESPACE = "WavePublisher";
constexpr auto TOPIC_KEY = "topic";
constexpr auto BUCKET_KEY = "bucket";
constexpr auto SITE_KEY = "site";
constexpr auto LOCATION_KEY = "location";

// ----------- InfluxDB settings
constexpr auto INFLUX_MEASUREMENT = "Sensors";
constexpr auto INFLUX_SENSOR = "WaveHeight";
constexpr uint16_t INFLUX_INTERVAL_S = 60;
constexpr uint8_t INFLUX_DECIMALS = 1;
constexpr uint8_t INFLUX_AVG_DEPTH_DECIMALS = 2;
constexpr size_t INFLUX_ROLLING_SAMPLES = 10;
constexpr uint8_t INFLUX_BATCH_SIZE = 3; // depth + enclosure + CPU points

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "DepthSensorBase.h"
#include "ESP32TempSensor.h"
#include "Influx.h"
#include "SerialX.h"
#include "SHT3xTempSensor.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "WiFiSettings.h"

//#define USE_ULTRASONIC
#define USE_MS5837

#ifdef USE_ULTRASONIC
#include "UltrasonicDepthSensor.h"

// ----------- Ultrasonic sensor pins
constexpr uint8_t TRIGGER_PIN = 10;
constexpr uint8_t ECHO_PIN = 11;
#endif

#ifdef USE_MS5837
#include "MS5837DepthSensor.h"
#endif

// ----------- Telemetry
constexpr uint8_t NUM_DECIMALS = 1;
constexpr uint16_t PUBLISH_INTERVAL_MS = 33; // 30 per sec
constexpr uint16_t SENSOR_INTERVAL_MS = 5000;
Timer publishTimer(PUBLISH_INTERVAL_MS);
Timer sensorTimer(SENSOR_INTERVAL_MS);

// ----------- LED wave height indicator
// The general-purpose LED (arduino.led) is dimmed to reflect the current wave height:
// off at/below LED_WAVE_HEIGHT_LOW_CM, full at/above LED_WAVE_HEIGHT_HIGH_CM, and
// linearly interpolated in between.
constexpr float LED_WAVE_HEIGHT_LOW_CM = -10.0f;
constexpr float LED_WAVE_HEIGHT_HIGH_CM = 10.0f;

// ----------- CPU throttling
constexpr uint8_t CPU_FREQUENCY_MHZ = 80; // keep things cool

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring. arduino itself implements IStatus and drives both the
// external RGB LED and the onboard NeoPixel, so status is visible even when the
// external LED isn't plugged in.
Arduino arduino;

#ifdef USE_ULTRASONIC
UltrasonicDepthSensor depthSensor(TRIGGER_PIN, ECHO_PIN);
#endif

#ifdef USE_MS5837
MS5837DepthSensor depthSensor;
#endif

DepthSensorBase* const depth = &depthSensor;

SHT3xTempSensor enclosureTemp;
ESP32TempSensor cpuTemp;

TelemetryEventHandler telemetryHandler(&arduino);
TelemetryPublisher* client = nullptr;
Influx* influx = nullptr;
InfluxPoint* devicePoint = nullptr;
InfluxField* averageDepthField = nullptr;
InfluxPoint* enclosurePoint = nullptr;
InfluxPoint* cpuPoint = nullptr;
InfluxField* enclosureTempField = nullptr;
InfluxField* enclosureHumidityField = nullptr;
InfluxField* cpuTempField = nullptr;

///
/// <summary>
/// Prints a WAVE_SITES entry as Influx="Bucket-Site-Location" Topic="topic".
/// </summary>
/// <param name="site">The entry to print.</param>
///
void printWaveSite(const WaveSite& site)
{
   Serial.print("Influx=\"");
   Serial.print(site.influxBucket);
   Serial.print("-");
   Serial.print(site.influxSite);
   Serial.print("-");
   Serial.print(site.influxLocation);
   Serial.print("\" Topic=\"");
   Serial.print(site.telemetryTopic);
   Serial.println("\"");
}

///
/// <summary>
/// Prompts the user over Serial to pick a wave site from WAVE_SITES and returns its
/// index. Blocks until a valid selection is entered.
/// </summary>
/// <returns>Index into WAVE_SITES for the chosen site.</returns>
///
uint8_t promptForSiteIndex()
{
   Serial.println("Select a wave site:");
   for (uint8_t i = 0; i < std::size(WAVE_SITES); i++)
   {
      Serial.print("  ");
      Serial.print(i + 1);
      Serial.print(": ");
      printWaveSite(WAVE_SITES[i]);
   }

   while (true)
   {
      Serial.print("Enter selection (1-");
      Serial.print(std::size(WAVE_SITES));
      Serial.print("): ");

      while (!Serial.available())
      {
         delay(10);
      }

      String input = Serial.readStringUntil('\n');
      input.trim();
      Serial.println(input);

      bool isNumeric = input.length() > 0;
      for (uint8_t i = 0; i < input.length(); i++)
      {
         if (!isDigit(input[i]))
         {
            isNumeric = false;
            break;
         }
      }

      if (isNumeric)
      {
         uint8_t selection = input.toInt();
         if (selection >= 1 && selection <= std::size(WAVE_SITES))
         {
            uint8_t index = selection - 1;
            Serial.print("Selected: ");
            printWaveSite(WAVE_SITES[index]);
            return index;
         }
      }

      Serial.println("Invalid selection, try again.");
   }
}

///
/// <summary>
/// Resolves which wave site/bucket to use: returns the topic/bucket/site/location saved
/// in Preferences, unless not all 4 values have been saved yet or forcePrompt is true, in
/// which case the user is prompted over Serial (from the WAVE_SITES list) and the choice
/// is saved for next time.
/// </summary>
/// <param name="forcePrompt">If true, always prompts even if a saved site exists. Used to
/// let callers detect buttonA being held at boot before other begin() calls run.</param>
/// <returns>The resolved WaveSite, backed by static storage.</returns>
///
WaveSite resolveSite(bool forcePrompt)
{
   static String topic, bucket, site, location;

   arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
   bool hasSavedSite = arduino.preferences.isKey(TOPIC_KEY) && arduino.preferences.isKey(BUCKET_KEY) && arduino.preferences.isKey(SITE_KEY) && arduino.preferences.isKey(LOCATION_KEY);
   if (hasSavedSite)
   {
      topic = arduino.preferences.getString(TOPIC_KEY);
      bucket = arduino.preferences.getString(BUCKET_KEY);
      site = arduino.preferences.getString(SITE_KEY);
      location = arduino.preferences.getString(LOCATION_KEY);
   }
   arduino.preferences.end();

   if (hasSavedSite && !forcePrompt)
   {
      return { topic.c_str(), bucket.c_str(), site.c_str(), location.c_str() };
   }

   const WaveSite& selected = WAVE_SITES[promptForSiteIndex()];
   topic = selected.telemetryTopic;
   bucket = selected.influxBucket;
   site = selected.influxSite;
   location = selected.influxLocation;

   arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   arduino.preferences.putString(TOPIC_KEY, topic);
   arduino.preferences.putString(BUCKET_KEY, bucket);
   arduino.preferences.putString(SITE_KEY, site);
   arduino.preferences.putString(LOCATION_KEY, location);
   arduino.preferences.end();

   return { topic.c_str(), bucket.c_str(), site.c_str(), location.c_str() };
}

void setup()
{
   SerialX::begin();
   Serial.print("Wave Publisher ");
   Serial.println(VERSION);

   arduino.begin(); // sets up the I2C bus/power rail and the RGB status LED

   // buttonA is on GPIO0, a strapping pin: holding it low during power-on/reset puts the
   // chip into UART download mode instead of running the sketch, so it can't be checked
   // during boot. Instead, give the user a short window after boot to press it.
   constexpr uint16_t FORCE_PROMPT_WINDOW_MS = 2000;
   Serial.println("Press buttonA now to reconfigure the wave site/bucket...");
   Timer forcePromptTimer(FORCE_PROMPT_WINDOW_MS);
   bool forcePrompt = false;
   while (!forcePromptTimer.ready())
   {
      if (arduino.buttonA.isPressed())
      {
         forcePrompt = true;
         break;
      }
   }

   // Resolved before initWifi/Influx so the chosen bucket is known before Influx is
   // constructed.
   WaveSite site = resolveSite(forcePrompt);

   // solid on while starting up; switches to wave-height-based fading in loop() once wave data is available
   arduino.led.turnOn(1.0f);

   arduino.initSensor("Enclosure Sensor", []() { return enclosureTemp.begin(); });
   arduino.initSensor("CPU Sensor", []() { return cpuTemp.begin(); });

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &arduino);
   arduino.enableOTA(VERSION, SKETCH_NAME);

   if (!arduino.initSensor("Depth Sensor", []() { return depth->begin(); }))
   {
      arduino.setStatus(Status::FAILED);
      delay(1000); // time for LED to show
      Util::reset();
   }

   influx = new Influx(INFLUX_INTERVAL_S, &arduino, INFLUXDB_URL, INFLUXDB_ORG, site.influxBucket);
   if (!influx->begin(arduino))
   {
      arduino.setStatus(Status::FAILED);
      delay(1000); // time for LED to show
      Util::reset();
   }

   // avgDepth isn't posted until the 5 minute averaging window is full (see loop())
   devicePoint = new InfluxPoint(INFLUX_MEASUREMENT, { { "site", site.influxSite }, { "location", site.influxLocation }, { "sensor", INFLUX_SENSOR } });
   averageDepthField = devicePoint->addValueField("avgDepth", INFLUX_AVG_DEPTH_DECIMALS);
   averageDepthField->setEnabled(false);

   enclosurePoint = new InfluxPoint(INFLUX_MEASUREMENT, { { "site", site.influxSite }, { "location", site.influxLocation }, { "sensor", INFLUX_SENSOR }, { "item", "Enclosure" } });
   cpuPoint = new InfluxPoint(INFLUX_MEASUREMENT, { { "site", site.influxSite }, { "location", site.influxLocation }, { "sensor", INFLUX_SENSOR }, { "item", "CPU" } });
   enclosureTempField = enclosurePoint->addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);
   enclosureHumidityField = enclosurePoint->addRollingAverageField(INFLUX_ROLLING_SAMPLES, "humidity", INFLUX_DECIMALS);
   cpuTempField = cpuPoint->addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);

   influx->client()->setWriteOptions(WriteOptions().batchSize(INFLUX_BATCH_SIZE).bufferSize(2 * INFLUX_BATCH_SIZE));

   arduino.enableRebooter();

   client = new TelemetryPublisher(site.telemetryTopic, NUM_DECIMALS, &arduino, &telemetryHandler);

   arduino.initClient("WebSocket", []() { client->beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &arduino);

   setCpuFrequencyMhz(CPU_FREQUENCY_MHZ);
}

void loop()
{
   if (client->isStarted())
   {
      // without a delay, the waveshare crashes
      delay(1);

      if (publishTimer.ready())
      {
         float distanceCM = depth->getDepth();
         float waveHeightCM = depth->getWaveHeight();
         Serial.print("Distance: ");
         Serial.print(distanceCM);
         Serial.print(" cm   Wave Height: ");
         Serial.print(waveHeightCM);
         Serial.println(" cm");
         client->setValue(distanceCM);

         float ledLevel = (waveHeightCM - LED_WAVE_HEIGHT_LOW_CM) / (LED_WAVE_HEIGHT_HIGH_CM - LED_WAVE_HEIGHT_LOW_CM);
         arduino.led.setLevel(constrain(ledLevel, 0.0f, 1.0f));

         // avgDepth isn't posted until the 5 minute averaging window is full, so it doesn't
         // report a partially-averaged value while the enclosure/CPU fields are already
         // posting on their own 60 second cadence.
         if (depth->isAverageFull())
         {
            averageDepthField->setEnabled(true);
            averageDepthField->set(depth->getAverageDepth());
         }
      }
   }

   client->loop(); // Continuously poll for events and maintain connection
   arduino.loop(); // Drives OTA update checks

   if (influx->ready())
   {
      devicePoint->post(influx->client(), true);
      enclosurePoint->post(influx->client(), true);
      cpuPoint->post(influx->client(), true);

      // All three points above were only queued into the write buffer (see INFLUX_BATCH_SIZE),
      // so flush now to post them together in a single HTTP request sharing one timestamp.
      if (!influx->client()->flushBuffer())
      {
         Serial.print("InfluxDB flush failed: ");
         Serial.println(influx->client()->getLastErrorMessage());
      }

      Serial.print("Enclosure temp: ");
      Serial.print(enclosureTemp.readTemperatureF());
      Serial.println(" °F");

      Serial.print("Enclosure humidity: ");
      Serial.print(enclosureTemp.readHumidity());
      Serial.println(" %");

      Serial.print("CPU temp: ");
      Serial.print(cpuTemp.readTemperatureF());
      Serial.println(" °F");
   }

   if (sensorTimer.ready())
   {
      enclosureTempField->set(enclosureTemp.readTemperatureF());
      enclosureHumidityField->set(enclosureTemp.readHumidity());
      cpuTempField->set(cpuTemp.readTemperatureF());
   }
}
