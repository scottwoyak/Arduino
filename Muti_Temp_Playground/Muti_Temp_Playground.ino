/// <summary>
/// Displays temperatures from up to 8 I2C-multiplexed sensors in a multi-column table (current
/// value plus 10s/1m/2m/10m time averages) and uploads all of those values to InfluxDB.
/// Hold Button A to show sensor type instead of the temperature table.
/// </summary>
/// <remarks>
/// Initializes up to 8 TempSensor instances behind an I2C multiplexer and tracks each sensor's
/// location metadata for telemetry tagging. Reads temperature on SENSOR_READ_INTERVAL_MS cadence
/// and feeds one current-value field plus four time-averaged fields (10s, 1m, 2m, 10m) per sensor,
/// while keeping upload cadence at INFLUX_INTERVAL_S. Renders either the temperature table or
/// sensor type labels (Button A held) on display.
/// 
/// Telemetry flow: each sensor maps to five InfluxPoints (current plus the four averaging windows),
/// all tagged with the sensor's location and a "stat" tag identifying which value they carry, each
/// posting a single "temperature" field. On each upload interval, all active sensor points are
/// posted, then the client buffer is flushed. Wi-Fi connectivity is monitored continuously and
/// triggers reset on loss.
/// 
/// Typical usage: start the sketch and verify detected sensors in Serial startup output, let averages
/// settle (up to 10 minutes for the longest window) before using displayed values for decisions,
/// then monitor the Influx stream for per-location current/averaged temperature values.
/// </remarks>

#include "ESP32_S3_Playground.h"

#include "TempSensor.h"
#include "SerialX.h"
#include "Influx.h"
#include "Status.h"
#include "I2CMultiplexor.h"
#include "Timer.h"
#include "DisplayGrid.h"
#include "DisplayField.h"

#include "WiFiSettings.h"

Format tempFormat(" ##.###");
Format uploadStatusFormat(7, Format::Alignment::RIGHT);

constexpr uint8_t NUM_SENSORS = 8;
constexpr uint8_t NUM_WINDOWS = 4;

constexpr uint8_t INFLUX_TEMP_DECIMAL_PLACES = 3;
constexpr uint8_t SENSOR_CORRECTION_DECIMAL_PLACES = 3;
constexpr uint8_t WIFI_RESET_DELAY_S = 10;
constexpr uint16_t SENSOR_READ_INTERVAL_MS = 500;
constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15;
constexpr auto INFLUX_TEMPERATURE_FIELD_NAME = "temperature";
constexpr auto INFLUX_STAT_TAG_NAME = "stat";
constexpr auto INFLUX_STAT_CURRENT = "current";

// Averaging windows, in seconds, displayed/uploaded alongside the current value
constexpr float AVERAGE_WINDOWS_S[NUM_WINDOWS] = { 10, 60, 120, 600 };
constexpr const char* AVERAGE_WINDOW_LABELS[NUM_WINDOWS] = { "10s", "1m", "2m", "10m" };

ESP32_S3_Playground arduino;
NeoPixelStatus status(&arduino.neoPixel);
I2CMultiplexor multi;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Influx influx(WIFI_SSID, WIFI_PASSWORD, &client, &status);
Timer sensorTimer(SENSOR_READ_INTERVAL_MS);
Timer influxTimer(INFLUX_INTERVAL_S * 1000);

TempSensor* sensors[NUM_SENSORS];
InfluxPoint* currentPoints[NUM_SENSORS];
InfluxField* currentFields[NUM_SENSORS];
InfluxPoint* averagePoints[NUM_SENSORS][NUM_WINDOWS];
InfluxField* averageFields[NUM_SENSORS][NUM_WINDOWS];
DisplayField* uploadStatusField = nullptr;

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

      currentPoints[i] = new InfluxPoint(INFLUX_MEASUREMENT);
      currentFields[i] = currentPoints[i]->addValueField(INFLUX_TEMPERATURE_FIELD_NAME, INFLUX_TEMP_DECIMAL_PLACES);
      currentPoints[i]->addTag("location", locations[i]);
      currentPoints[i]->addTag(INFLUX_STAT_TAG_NAME, INFLUX_STAT_CURRENT);

      for (uint8_t w = 0; w < NUM_WINDOWS; w++)
      {
         averagePoints[i][w] = new InfluxPoint(INFLUX_MEASUREMENT);
         averageFields[i][w] = averagePoints[i][w]->addTimeAveragedField(AVERAGE_WINDOWS_S[w], INFLUX_TEMPERATURE_FIELD_NAME, INFLUX_TEMP_DECIMAL_PLACES);
         averagePoints[i][w]->addTag("location", locations[i]);
         averagePoints[i][w]->addTag(INFLUX_STAT_TAG_NAME, AVERAGE_WINDOW_LABELS[w]);
      }
   }

   arduino.begin();
   arduino.setTextSize(2);
   arduino.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   status.begin();
   status.setStatus(Status::STARTED);

   arduino.echoToSerial = true;
   arduino.clearDisplay();
   arduino.println("Initializing", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 2);

   arduino.print("Sensors... ", Color::LABEL);
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
   arduino.printlnR("ok", Color::VALUE);

   constexpr uint16_t NUM_POINTS = NUM_SENSORS * (1 + NUM_WINDOWS);
   client.setWriteOptions(WriteOptions().batchSize(NUM_POINTS).bufferSize(NUM_POINTS).flushInterval(INFLUX_INTERVAL_S + 1));
   if (!influx.begin(&arduino))
   {
      Util::reset(WIFI_RESET_DELAY_S);
   }

       arduino.clearDisplay();
       arduino.echoToSerial = false;

       arduino.setTextSize(2);
       std::string uploadSample(uploadStatusFormat.length(), '0');
       int16_t uploadX = arduino.width() - arduino.textWidth(uploadSample.c_str());
       int16_t uploadY = arduino.height() - arduino.charH();
       uploadStatusField = new DisplayField(&arduino, uploadX, uploadY, "", uploadStatusFormat, 2, true, Color::GRAY, Color::GRAY);
       uploadStatusField->setValue("");
       uploadStatusField->draw();
   }

void loop()
{
   const bool showType = arduino.buttonA.isPressed();
   static bool lastShowType = showType;

   if (showType != lastShowType)
   {
      arduino.clearDisplay();
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
         float tempF = sensors[i]->readTemperatureF();
         currentFields[i]->set(tempF);
         for (uint8_t w = 0; w < NUM_WINDOWS; w++)
         {
            averageFields[i][w]->set(tempF);
         }
      }
   }

   if (!influx.ensureWiFiConnected())
   {
      arduino.println("WiFi connection lost");
      Serial.println("WiFi connection lost");
      Util::reset(WIFI_RESET_DELAY_S);
   }

   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.print("Mutli Temperature Monitor", Color::HEADING);
   arduino.println();
   arduino.setTextSize(2);
   arduino.moveCursorY(arduino.charH() / 3);

   if (showType)
   {
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         arduino.print(i, Color::GRAY);
         arduino.print(" ");

         if (!sensors[i]->exists())
         {
            arduino.println("----", Color::GRAY);
            continue;
         }

         arduino.println(sensors[i]->type(), Color::VALUE);
      }
   }
   else
   {
      static Format idFormat(" ##", Format::Alignment::RIGHT);
      static const DisplayGrid::Column columns[] = {
         { "Sensor", &idFormat },
         { "Now", &tempFormat },
         { AVERAGE_WINDOW_LABELS[0], &tempFormat },
         { AVERAGE_WINDOW_LABELS[1], &tempFormat },
         { AVERAGE_WINDOW_LABELS[2], &tempFormat },
         { AVERAGE_WINDOW_LABELS[3], &tempFormat },
      };
      DisplayGrid grid(&arduino, nullptr, columns, 6, Color::WHITE);
      grid.printHeader();

      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (!sensors[i]->exists())
         {
            grid.printRow(Color::GRAY, i, "----", "----", "----", "----", "----");
            continue;
         }

         grid.printRow(Color::VALUE, i, currentFields[i]->get(), averageFields[i][0]->get(), averageFields[i][1]->get(),
                       averageFields[i][2]->get(), averageFields[i][3]->get());
      }
   }

   if (influxTimer.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      uploadStatusField->setValue("Upload");
      uploadStatusField->draw();

      bool writeFailed = false;
      for (uint8_t i = 0; i < NUM_SENSORS && !writeFailed; i++)
      {
         if (!sensors[i]->exists())
         {
            continue;
         }

         if (!currentPoints[i]->post(&client))
         {
            writeFailed = true;
            break;
         }

         for (uint8_t w = 0; w < NUM_WINDOWS; w++)
         {
            if (!averagePoints[i][w]->post(&client))
            {
               writeFailed = true;
               break;
            }
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
             uploadStatusField->setValue("");
             uploadStatusField->draw();
          }
      }


