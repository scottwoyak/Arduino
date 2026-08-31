//
// Wind Publisher
//
// Reads wind speed from an anemometer and publishes live readings over a WebSocket
// telemetry connection, while also uploading rolling-averaged enclosure and CPU
// temperature/humidity readings to InfluxDB on a fixed interval.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   streams live wind speed readings as they're read.
// - Samples enclosure temperature/humidity and CPU temperature every SENSOR_INTERVAL_MS
//   and accumulates rolling averages for the next InfluxDB upload.
// - Prints wind speed and temperature readings to Serial every SERIAL_INTERVAL_MS.
// - Posts telemetry to InfluxDB every INFLUX_INTERVAL_S seconds.
// - Resets the device on telemetry disconnect or error.
// - Restarts the device every 24 hours to play it safe.
// - Checks for a firmware update periodically.
//
// Telemetry topic / InfluxDB site selection:
// - The telemetry topic (and matching InfluxDB site/location/bucket) is one of the 3
//   entries in WIND_SITES: the Lake and Bragg sites (both using the "Monitor" bucket),
//   plus a Test entry (topic "Wind/Test", site/location "Test", bucket "Testing") for
//   testing without touching production data. The choice is made once and saved to
//   Preferences (NVS) so it survives OTA firmware updates.
// - On startup, if no selection has been saved yet, or if buttonA (the onboard BOOT
//   button) is held down while the sketch starts, the user is prompted over Serial to
//   pick an entry from the WIND_SITES list; the choice is then saved to Preferences.
//
// InfluxDB points uploaded (Measurement: Sensors, bucket=<selected>):
//
// - site=<selected>, location=<selected>, sensor=Wind, item=Enclosure
//     temperature: rolling average of enclosureTemp.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//     humidity: rolling average of enclosureTemp.readHumidity(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//
// - site=<selected>, location=<selected>, sensor=Wind, item=CPU
//     temperature: rolling average of cpuTemp.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

#include <iterator>

constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Wind_Publisher";

// ----------- Telemetry topic / InfluxDB site selection
struct WindSite
{
   const char* telemetryTopic;
   const char* influxBucket;
   const char* influxSite;
   const char* influxLocation;
};

constexpr WindSite WIND_SITES[] = {
   { "Wind/Lake", "Monitor", "Lake", "Dock" },
   { "Wind/Bragg", "Monitor", "Bragg", "Studio" },
   { "Wind/Test", "Testing", "Test", "Test" },
};

constexpr auto PREFERENCES_NAMESPACE = SKETCH_NAME;
constexpr auto TOPIC_KEY = "topic";
constexpr auto BUCKET_KEY = "bucket";
constexpr auto SITE_KEY = "site";
constexpr auto LOCATION_KEY = "location";

// ----------- InfluxDB settings
constexpr auto INFLUX_MEASUREMENT = "Sensors";
constexpr auto INFLUX_SENSOR = "Wind";
constexpr uint16_t INFLUX_INTERVAL_S = 60;
constexpr uint8_t INFLUX_DECIMALS = 2;
constexpr size_t INFLUX_ROLLING_SAMPLES = 10;
constexpr uint8_t INFLUX_BATCH_SIZE = 2; // enclosure + CPU points

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "ESP32TempSensor.h"
#include "Influx.h"
#include "SerialX.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "TempSensor.h"
#include "Timer.h"
#include "WindMeter.h"

#include "WiFiSettings.h"

// ----------- Telemetry
constexpr uint8_t NUM_DECIMALS = 2;
constexpr uint16_t SERIAL_INTERVAL_MS = 5000;
constexpr uint16_t SENSOR_INTERVAL_MS = 100;
Timer serialTimer(SERIAL_INTERVAL_MS);
Timer sensorTimer(SENSOR_INTERVAL_MS);

// ----------- Wind sensor pins
constexpr uint8_t WIND_SENSOR_PIN = 11;
constexpr uint8_t WIND_SENSOR_GROUND_PIN = 13; // held LOW to power the wind encoder/sensor
constexpr uint8_t WIND_SENSOR_POWER_PIN = 12; // held HIGH to power the wind encoder/sensor

// ----------- CPU throttling
constexpr uint8_t CPU_FREQUENCY_MHZ = 80; // keep things cool

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring.
Arduino arduino;
WindMeter wind(WIND_SENSOR_PIN, arduino.ledPin(), LEDColor::CLEAR_PINK);

// Constructed in setup(), once the InfluxDB bucket has been resolved.
Influx* influx = nullptr;

TempSensor enclosureTemp;
ESP32TempSensor cpuTemp;

TelemetryEventHandler telemetryHandler(&arduino);

// Constructed in setup(), once the telemetry topic/Influx site has been resolved.
TelemetryPublisher* client = nullptr;
InfluxPoint* enclosurePoint = nullptr;
InfluxPoint* cpuPoint = nullptr;
InfluxField* enclosureTempField = nullptr;
InfluxField* enclosureHumidityField = nullptr;
InfluxField* cpuTempField = nullptr;

///
/// <summary>
/// Prints a WIND_SITES entry as Influx="Bucket-Site-Location" Topic="topic".
/// </summary>
/// <param name="site">The entry to print.</param>
///
void printWindSite(const WindSite& site)
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
/// Prompts the user over Serial to pick a wind site from WIND_SITES and returns its
/// index. Blocks until a valid selection is entered.
/// </summary>
/// <returns>Index into WIND_SITES for the chosen site.</returns>
///
uint8_t promptForSiteIndex()
{
   Serial.println("Select a wind site:");
   for (uint8_t i = 0; i < std::size(WIND_SITES); i++)
   {
      Serial.print("  ");
      Serial.print(i + 1);
      Serial.print(": ");
      printWindSite(WIND_SITES[i]);
   }

   while (true)
   {
      Serial.print("Enter selection (1-");
      Serial.print(std::size(WIND_SITES));
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
         if (selection >= 1 && selection <= std::size(WIND_SITES))
         {
            uint8_t index = selection - 1;
            Serial.print("Selected: ");
            printWindSite(WIND_SITES[index]);
            return index;
         }
      }

      Serial.println("Invalid selection, try again.");
   }
}

///
/// <summary>
/// Resolves which wind site/bucket to use: returns the topic/bucket/site/location saved
/// in Preferences, unless not all 4 values have been saved yet or forcePrompt is true, in
/// which case the user is prompted over Serial (from the WIND_SITES list) and the choice
/// is saved for next time.
/// </summary>
/// <param name="forcePrompt">If true, always prompts even if a saved site exists. Used to
/// let callers detect buttonA being held at boot before other begin() calls run.</param>
/// <returns>The resolved WindSite, backed by static storage.</returns>
///
WindSite resolveSite(bool forcePrompt)
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

   const WindSite& selected = WIND_SITES[promptForSiteIndex()];
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
   arduino.addLogger(new SerialLogger());

   Serial.print("Wind Publisher ");
   Serial.println(VERSION);

   // power the wind encoder/sensor
   pinMode(WIND_SENSOR_GROUND_PIN, OUTPUT);
   pinMode(WIND_SENSOR_POWER_PIN, OUTPUT);
   digitalWrite(WIND_SENSOR_GROUND_PIN, LOW);
   digitalWrite(WIND_SENSOR_POWER_PIN, HIGH);

   arduino.begin(); // sets up the I2C bus/power rail and the RGB status LED

   // buttonA is on GPIO0, a strapping pin: holding it low during power-on/reset puts the
   // chip into UART download mode instead of running the sketch, so it can't be checked
   // during boot. Instead, give the user a short window after boot to press it.
   constexpr uint16_t FORCE_PROMPT_WINDOW_MS = 2000;
   Serial.println("Press buttonA now to reconfigure the wind site/bucket...");
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

   enclosureTemp.begin();
   cpuTemp.begin();

   wind.begin();

   // Resolved before initWifi/Influx so the chosen bucket is known before Influx is
   // constructed.
   WindSite site = resolveSite(forcePrompt);

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &arduino);
   arduino.enableRebooter();
   arduino.enableOTA(VERSION, SKETCH_NAME);

   influx = new Influx(INFLUX_INTERVAL_S, &arduino, INFLUXDB_URL, INFLUXDB_ORG, site.influxBucket);
   if (!influx->begin(arduino))
   {
      arduino.setStatus(Status::FAILED);
      delay(1000); // time for LED to show
      Util::reset();
   }

   influx->client()->setWriteOptions(WriteOptions().batchSize(INFLUX_BATCH_SIZE).bufferSize(2 * INFLUX_BATCH_SIZE));

   client = new TelemetryPublisher(site.telemetryTopic, NUM_DECIMALS, &arduino, &telemetryHandler);

   enclosurePoint = new InfluxPoint(INFLUX_MEASUREMENT, { { "site", site.influxSite }, { "location", site.influxLocation }, { "sensor", INFLUX_SENSOR }, { "item", "Enclosure" } });
   cpuPoint = new InfluxPoint(INFLUX_MEASUREMENT, { { "site", site.influxSite }, { "location", site.influxLocation }, { "sensor", INFLUX_SENSOR }, { "item", "CPU" } });
   enclosureTempField = enclosurePoint->addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);
   enclosureHumidityField = enclosurePoint->addRollingAverageField(INFLUX_ROLLING_SAMPLES, "humidity", INFLUX_DECIMALS);
   cpuTempField = cpuPoint->addRollingAverageField(INFLUX_ROLLING_SAMPLES, "temperature", INFLUX_DECIMALS);

   arduino.initClient("WebSocket", []() { client->beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &arduino);

   setCpuFrequencyMhz(CPU_FREQUENCY_MHZ);

   arduino.clearLoggers();
}

void loop()
{
   if (client->isStarted())
   {
      // without a delay, the waveshare crashes
      delay(1);

      float speed = wind.getSpeed();
      client->setValue(speed);
   }

   client->loop(); // Continuously poll for events and maintain connection
   arduino.loop(); // Drives OTA update checks

   if (sensorTimer.ready())
   {
      enclosureTempField->set(enclosureTemp.readTemperatureF());
      enclosureHumidityField->set(enclosureTemp.readHumidity());
      cpuTempField->set(cpuTemp.readTemperatureF());
   }

   if (serialTimer.ready())
   {
      Serial.print("Wind speed: ");
      Serial.print(wind.getSpeed());
      Serial.println(" m/s");

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

   if (client->isStarted() && influx->ready())
   {
      enclosurePoint->post(influx->client(), true);
      cpuPoint->post(influx->client(), true);

      // Both points above were only queued into the write buffer (see INFLUX_BATCH_SIZE),
      // so flush now to post them together in a single HTTP request sharing one timestamp.
      if (!influx->client()->flushBuffer())
      {
         Serial.print("InfluxDB flush failed: ");
         Serial.println(influx->client()->getLastErrorMessage());
      }
   }
}

