// -------------------------------------------------------------------------------------------------
//
// Displays temperatures from up to 8 I2C-multiplexed sensors and uploads readings to InfluxDB.
// Hold Button A to show sensor type instead of temperature for each sensor index.
//
// Detailed behavior:
// - Initializes up to 8 TempSensor instances behind an I2C multiplexer and tracks each sensor's
//   location metadata for telemetry tagging.
// - Reads temperature/humidity on SENSOR_READ_INTERVAL_MS cadence and feeds time-averaged fields.
// - Uses a 60-second averaging window for displayed/uploaded values while keeping upload cadence
//   at INFLUX_INTERVAL_S.
// - Renders either averaged temperature values or sensor type labels (Button A held) on display.
//
// Telemetry flow:
// - Each sensor maps to a SimplePoint with averaged temperature/humidity fields.
// - On each upload interval, all active sensor points are posted, then client buffer is flushed.
// - Wi-Fi connectivity is monitored continuously and triggers reset on loss.
//
// Typical usage:
// - Start sketch and verify detected sensors in Serial startup output.
// - Let averages settle (up to 60 seconds) before using displayed values for decisions.
// - Monitor Influx stream for per-location averaged temperature/humidity values.
//
// -------------------------------------------------------------------------------------------------
#include "Feather.h"
#include "TempSensor.h"
#include "SerialX.h"
#include "Influx.h"
#include "Status.h"
#include "I2CMultiplexor.h"
#include "Timer.h"

#include "WiFiSettings.h"

Format tempFormat("###.## F");

constexpr uint8_t NUM_SENSORS = 8;

constexpr uint8_t INFLUX_TEMP_DECIMAL_PLACES = 3;
constexpr uint8_t INFLUX_HUMIDITY_DECIMAL_PLACES = 2;
constexpr uint8_t SENSOR_CORRECTION_DECIMAL_PLACES = 3;
constexpr uint8_t WIFI_RESET_DELAY_S = 10;
constexpr uint16_t SENSOR_READ_INTERVAL_MS = 500;
constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15;
constexpr auto AVERAGE_WINDOW_S = 60;
constexpr auto INFLUX_TEMPERATURE_FIELD_NAME = "temperature";
constexpr auto INFLUX_HUMIDITY_FIELD_NAME = "humidity";

Feather feather;
NeoPixelStatus status(&feather.neoPixel);
I2CMultiplexor multi;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Influx influx(WIFI_SSID, WIFI_PASSWORD, &client, &status);
Timer sensorTimer(SENSOR_READ_INTERVAL_MS);
Timer influxTimer(INFLUX_INTERVAL_S * 1000);

TempSensor* sensors[NUM_SENSORS];
SimplePoint* points[NUM_SENSORS];
Field* tempFields[NUM_SENSORS];
Field* humFields[NUM_SENSORS];

const char* locations[NUM_SENSORS] = {
   "Test 1",
   "Test 2",
   "Test 3",
   "Test 4",
   "Test 5",
   "Test 6",
   "Test 7",
   "Test 8",
};

void setup()
{
   SerialX::begin();
   Wire.begin();

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensors[i] = new TempSensor();
      points[i] = new SimplePoint(INFLUX_MEASUREMENT);
      tempFields[i] = points[i]->addTimeAveragedField(AVERAGE_WINDOW_S, INFLUX_TEMPERATURE_FIELD_NAME, INFLUX_TEMP_DECIMAL_PLACES);
      humFields[i] = points[i]->addTimeAveragedField(AVERAGE_WINDOW_S, INFLUX_HUMIDITY_FIELD_NAME, INFLUX_HUMIDITY_DECIMAL_PLACES);
      points[i]->addTag("location", locations[i]);
   }

   feather.begin();
   feather.setRotation(DisplayRotation::PORTRAIT);
   feather.setTextSize(2);
   feather.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   status.begin();
   status.setStatus(Status::STARTED);

   feather.echoToSerial = true;
   feather.clearDisplay();
   feather.println("Initializing", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.print("Sensors... ", Color::LABEL);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      Serial.println();
      Serial.print("Sensor ");
      Serial.print(i);
      Serial.print(": ");
      Serial.println(locations[i]);

      multi.select(i);
      if (sensors[i]->begin(true))
      {
         if (!sensors[i]->exists())
         {
            Serial.println("Sensor not detected");
         }
         else
         {
            Serial.print("         Type: ");
            Serial.println(sensors[i]->type());
            Serial.print("      Address: ");
            Serial.println(sensors[i]->address());
            Serial.print("           ID: ");
            Serial.println(sensors[i]->id());
            Serial.print("   Correction: ");
            Serial.println(sensors[i]->tempCorrectionF(), SENSOR_CORRECTION_DECIMAL_PLACES);
         }
      }
      else
      {
         status.setStatus(Color::RED);
         Serial.println("FAILED");
      }
   }
   feather.printlnR("ok", Color::VALUE);

   client.setWriteOptions(WriteOptions().batchSize(NUM_SENSORS).bufferSize(NUM_SENSORS).flushInterval(INFLUX_INTERVAL_S + 1));
   if (!influx.begin(&feather))
   {
      Util::reset(WIFI_RESET_DELAY_S);
   }

   feather.clearDisplay();
   feather.echoToSerial = false;
}

void loop()
{
   const bool showType = feather.buttonA.isPressed();
   static bool lastShowType = showType;

   if (showType != lastShowType)
   {
      feather.clearDisplay();
      lastShowType = showType;
   }

   if (sensorTimer.ready())
   {
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (!sensors[i]->exists())
         {
            continue;
         }

         multi.select(i);
         tempFields[i]->set(sensors[i]->readTemperatureF());
         humFields[i]->set(sensors[i]->readHumidity());
      }
   }

   if (!influx.ensureWiFiConnected())
   {
      feather.println("WiFi connection lost");
      Serial.println("WiFi connection lost");
      Util::reset(WIFI_RESET_DELAY_S);
   }

   feather.setCursor(0, 0);
   feather.setTextSize(2);
   feather.print("Multi Monitor", Color::HEADING);
   feather.println();
   feather.moveCursorY(feather.charH() / 3);

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      feather.setTextSize(2);
      feather.print(i, Color::GRAY);
      feather.print(" ");
      feather.setTextSize(3);

      if (!sensors[i]->exists())
      {
         feather.println("----", Color::GRAY);
         continue;
      }

      if (showType)
      {
         feather.println(sensors[i]->type(), Color::VALUE);
      }
      else
      {
         float temp = tempFields[i]->get();
         feather.println(temp, tempFormat, Color::VALUE);
      }
   }

   if (influxTimer.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);

      bool writeFailed = false;
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (!sensors[i]->exists())
         {
            continue;
         }

         if (!points[i]->post(&client))
         {
            writeFailed = true;
            break;
         }
      }

      if (!writeFailed && !client.isBufferEmpty())
      {
         writeFailed = !client.flushBuffer();
      }

      if (writeFailed)
      {
         Serial.println("InfluxDB write failed: ");
         Serial.println(client.getLastErrorMessage());
      }

      digitalWrite(BUILTIN_LED, LOW);
   }
}


