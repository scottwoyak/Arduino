//
// Gate Publisher
//
// Reads the compass azimuth from a QMC5883P magnetometer (as found on HW-127/GY-273
// breakouts) and publishes live readings over a WebSocket telemetry connection, while
// also uploading a rolling-averaged CPU temperature reading to InfluxDB on a fixed
// interval.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   streams live azimuth readings as they're read.
// - Samples CPU temperature every SENSOR_INTERVAL_MS and accumulates a rolling average
//   for the next InfluxDB upload.
// - Posts telemetry to InfluxDB every INFLUX_INTERVAL_S seconds.
// - Resets the device on telemetry disconnect or error.
// - Restarts the device every 24 hours to play it safe.
// - Checks for a firmware update periodically.
//
// Telemetry topic / InfluxDB location selection:
// - The site is always "Bragg", but there are two gate locations: Left and Right. The
//   telemetry topic (and matching InfluxDB location) is one of the 2 entries in
//   GATE_LOCATIONS, plus a Test entry (topic "Gate/Test", location "Test", bucket
//   "Testing") for testing without touching production data. The choice is made once
//   and saved to Preferences (NVS) so it survives OTA firmware updates.
// - On startup, if no selection has been saved yet, or if buttonA (the onboard BOOT
//   button) is held down while the sketch starts, the user is prompted over Serial to
//   pick an entry from the GATE_LOCATIONS list; the choice is then saved to
//   Preferences.
//
// InfluxDB points uploaded (Measurement: Sensors, bucket=<selected>):
//
// - site=Bragg, location=<selected>, sensor=Gate, item=CPU
//     temperature: rolling average of cpuTemp.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//
// Note: azimuth is only streamed live over telemetry, not uploaded to InfluxDB.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

#include <iterator>

constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Gate_Publisher";

// ----------- Telemetry topic / InfluxDB location selection
struct GateLocation
{
   const char* telemetryTopic;
   const char* influxBucket;
   const char* influxSite;
   const char* influxLocation;
};

constexpr GateLocation GATE_LOCATIONS[] = {
   { "Gate/Left", "Monitor", "Bragg", "Left" },
   { "Gate/Right", "Monitor", "Bragg", "Right" },
   { "Gate/Test", "Testing", "Test", "Test" },
};

constexpr auto PREFERENCES_NAMESPACE = SKETCH_NAME;
constexpr auto TOPIC_KEY = "topic";
constexpr auto BUCKET_KEY = "bucket";
constexpr auto SITE_KEY = "site";
constexpr auto LOCATION_KEY = "location";

// ----------- InfluxDB settings
constexpr auto INFLUX_MEASUREMENT = "Sensors";
constexpr auto INFLUX_SENSOR = "Gate";
constexpr uint16_t INFLUX_INTERVAL_S = 60;
constexpr uint8_t INFLUX_DECIMALS = 2;
constexpr size_t INFLUX_ROLLING_SAMPLES = 10;
constexpr uint8_t INFLUX_BATCH_SIZE = 1; // CPU point

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "ESP32TempSensor.h"
#include "Influx.h"
#include "QMC5883PMagnometer.h"
#include "SerialX.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "Timer.h"

#include "WiFiSettings.h"

// ----------- Telemetry
constexpr uint8_t NUM_DECIMALS = 2;
constexpr uint16_t SENSOR_INTERVAL_MS = 100;
Timer sensorTimer(SENSOR_INTERVAL_MS);

// ----------- CPU throttling
constexpr uint8_t CPU_FREQUENCY_MHZ = 80; // keep things cool

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring.
Arduino arduino;
QMC5883PMagnometer magnetometer;

// Constructed in setup(), once the InfluxDB bucket has been resolved.
Influx* influx = nullptr;

ESP32TempSensor cpuTemp;

TelemetryEventHandler telemetryHandler(&arduino);

// Constructed in setup(), once the telemetry topic/Influx location has been resolved.
TelemetryPublisher* client = nullptr;
InfluxPoint* cpuPoint = nullptr;
InfluxField* cpuTempField = nullptr;

///
/// <summary>
/// Formats a GATE_LOCATIONS entry as Influx="Bucket-Site-Location" Topic="topic".
/// </summary>
/// <param name="location">The entry to format.</param>
/// <returns>The formatted description.</returns>
///
String describeGateLocation(const GateLocation& location)
{
   return String("Influx=\"") + location.influxBucket + "-" + location.influxSite + "-" + location.influxLocation + "\" Topic=\"" + location.telemetryTopic + "\"";
}

///
/// <summary>
/// Prompts the user over Serial to pick a gate location from GATE_LOCATIONS and returns
/// its index. Blocks until a valid selection is entered.
/// </summary>
/// <returns>Index into GATE_LOCATIONS for the chosen location.</returns>
///
uint8_t promptForLocationIndex()
{
   String options[std::size(GATE_LOCATIONS)];
   for (uint8_t i = 0; i < std::size(GATE_LOCATIONS); i++)
   {
      options[i] = describeGateLocation(GATE_LOCATIONS[i]);
   }

   uint8_t index = SerialX::promptForOption("Select a gate location:", options, std::size(options));

   Serial.print("Selected: ");
   Serial.println(options[index]);

   return index;
}

///
/// <summary>
/// Resolves which gate location/bucket to use: returns the topic/bucket/site/location
/// saved in Preferences, unless not all 4 values have been saved yet or forcePrompt is
/// true, in which case the user is prompted over Serial (from the GATE_LOCATIONS list)
/// and the choice is saved for next time.
/// </summary>
/// <param name="forcePrompt">If true, always prompts even if a saved location exists.
/// Used to let callers detect buttonA being held at boot before other begin() calls
/// run.</param>
/// <returns>The resolved GateLocation, backed by static storage.</returns>
///
GateLocation resolveLocation(bool forcePrompt)
{
   static String topic, bucket, site, location;

   arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
   bool hasSavedLocation = arduino.preferences.isKey(TOPIC_KEY) && arduino.preferences.isKey(BUCKET_KEY) && arduino.preferences.isKey(SITE_KEY) && arduino.preferences.isKey(LOCATION_KEY);
   if (hasSavedLocation)
   {
      topic = arduino.preferences.getString(TOPIC_KEY);
      bucket = arduino.preferences.getString(BUCKET_KEY);
      site = arduino.preferences.getString(SITE_KEY);
      location = arduino.preferences.getString(LOCATION_KEY);
   }
   arduino.preferences.end();

   if (hasSavedLocation && !forcePrompt)
   {
      return { topic.c_str(), bucket.c_str(), site.c_str(), location.c_str() };
   }

   const GateLocation& selected = GATE_LOCATIONS[promptForLocationIndex()];
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

   Serial.print("Gate Publisher ");
   Serial.println(VERSION);

   arduino.begin(); // sets up the I2C bus/power rail and the RGB status LED

   // buttonA is on GPIO0, a strapping pin: holding it low during power-on/reset puts the
   // chip into UART download mode instead of running the sketch, so it can't be checked
   // during boot. Instead, give the user a short window after boot to press it.
   constexpr uint16_t FORCE_PROMPT_WINDOW_MS = 2000;
   Serial.println("Press buttonA now to reconfigure the gate location/bucket...");
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

   if (!magnetometer.begin())
   {
      Serial.println("QMC5883P Not Found (no I2C ACK - check wiring)");
      arduino.setStatus(Status::FAILED);
      Util::reset(10);
   }

   cpuTemp.begin();

   // Resolved before initWifi/Influx so the chosen bucket is known before Influx is
   // constructed.
   GateLocation location = resolveLocation(forcePrompt);

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &arduino);
   arduino.enableRebooter();
   arduino.enableOTA(VERSION, SKETCH_NAME);

   influx = new Influx(INFLUX_INTERVAL_S, &arduino, INFLUXDB_URL, INFLUXDB_ORG, location.influxBucket);
   if (!influx->begin(arduino))
   {
      arduino.setStatus(Status::FAILED);
      delay(1000); // time for LED to show
      Util::reset();
   }

   influx->client()->setWriteOptions(WriteOptions().batchSize(INFLUX_BATCH_SIZE).bufferSize(2 * INFLUX_BATCH_SIZE));

   client = new TelemetryPublisher(location.telemetryTopic, NUM_DECIMALS, &arduino, &telemetryHandler);

   cpuPoint = new InfluxPoint(INFLUX_MEASUREMENT, { { "site", location.influxSite }, { "location", location.influxLocation }, { "sensor", INFLUX_SENSOR }, { "item", "CPU" } });
   cpuTempField = cpuPoint->addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);

   arduino.initClient("WebSocket", []() { client->beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &arduino);

   setCpuFrequencyMhz(CPU_FREQUENCY_MHZ);
}

void loop()
{
   magnetometer.update();

   if (client->isStarted())
   {
      // without a delay, the waveshare crashes
      delay(1);

      client->setValue(magnetometer.azimuth());
   }

   client->loop(); // Continuously poll for events and maintain connection
   arduino.loop(); // Drives OTA update checks

   if (sensorTimer.ready())
   {
      cpuTempField->set(cpuTemp.readTemperatureF());
   }

   if (client->isStarted() && influx->ready())
   {
      if (!cpuPoint->post(influx->client(), true))
      {
         Serial.print("InfluxDB post failed: ");
         Serial.println(influx->client()->getLastErrorMessage());
      }
   }
}
