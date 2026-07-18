/// <summary>
/// Displays temperatures from up to 8 I2C-multiplexed sensors in a multi-column table (current
/// value plus 10s/1m/2m/5m/10m time averages) and uploads all of those values to InfluxDB.
/// Hold Button A to show sensor type instead of the temperature table. Rotate encoderA to
/// cycle between the table and a scatterplot of the last 100 samples for each sampling rate
/// (current value plus each averaging window).
/// </summary>
/// <remarks>
/// Initializes up to 8 TempSensor instances behind an I2C multiplexer and tracks each sensor's
/// location metadata for telemetry tagging. Reads temperature on SENSOR_READ_INTERVAL_MS cadence
/// and feeds one current-value field plus five time-averaged fields (10s, 1m, 2m, 5m, 10m) per sensor,
/// while keeping upload cadence at INFLUX_INTERVAL_S. Renders the temperature table, sensor type
/// labels (Button A held), or one of six per-sensor scatterplot views (encoderA). Only the plot
/// view currently being displayed is allocated in memory; its TimedScatterPlot and per-sensor
/// series are created when switching to that view and destroyed when leaving it, so memory is
/// bounded by a single view's series rather than by all views combined.
/// 
/// Telemetry flow: each sensor maps to five InfluxPoints (current plus the four averaging windows),
/// all tagged with the sensor's location and a "stat" tag identifying which value they carry, each
/// posting a single "temperature" field. On each upload interval, all active sensor points are
/// posted individually. Wi-Fi connectivity is monitored continuously and triggers reset on loss.
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
#include "Util.h"

#include "WiFiSettings.h"
#include "TimedScatterPlot.h"

Format tempFormat(" ##.###");
Format uploadStatusFormat(7, Format::Alignment::RIGHT);

constexpr uint8_t NUM_SENSORS = 8;
constexpr uint8_t NUM_WINDOWS = 5;

// Views cycled via encoderA: the temperature table, then one scatterplot per
// sampling rate (current value plus each averaging window). Only one plot view's
// TimedScatterPlot/series are ever allocated at a time (see _activatePlotView()),
// created on demand when switching to a plot view and destroyed when leaving it, so
// memory is bounded by a single view's series instead of NUM_VIEWS worth at once.
enum class ViewMode : uint8_t
{
   TABLE = 0,
   NOW,
   WINDOW_0,
   WINDOW_1,
   WINDOW_2,
   WINDOW_3,
   WINDOW_4,
};
constexpr uint8_t NUM_VIEWS = static_cast<uint8_t>(ViewMode::WINDOW_4) + 1;
constexpr uint8_t NUM_PLOT_VIEWS = NUM_VIEWS - 1;

// Distinct colors used to tell sensors apart on the scatterplots.
constexpr Color SENSOR_PLOT_COLORS[NUM_SENSORS] = {
   Color::YELLOW, Color::CYAN, Color::GREEN, Color::MAGENTA,
   Color::ORANGE, Color::RED, Color::WHITE, Color::PINK,
};

constexpr uint8_t INFLUX_TEMP_DECIMAL_PLACES = 3;
constexpr uint8_t SENSOR_CORRECTION_DECIMAL_PLACES = 3;
constexpr uint8_t WIFI_RESET_DELAY_S = 10;
constexpr uint16_t SENSOR_READ_INTERVAL_MS = 500;
constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 10;
constexpr auto INFLUX_TEMPERATURE_FIELD_NAME = "temperature";
constexpr auto INFLUX_STAT_TAG_NAME = "stat";
constexpr auto INFLUX_STAT_CURRENT = "current";

// Every uploadAllPoints() call queues up to this many points (one "current" point plus
// one point per averaging window, per sensor). Setting the write batch size to this
// count means writePoint() only buffers each point instead of sending it immediately;
// they are all still separate Influx points/timestamps, just posted together in a
// single HTTP request when flushed at the end of uploadAllPoints().
constexpr uint8_t INFLUX_POINTS_PER_SENSOR = NUM_WINDOWS + 1;
constexpr uint8_t INFLUX_BATCH_SIZE = NUM_SENSORS * INFLUX_POINTS_PER_SENSOR;

// Averaging windows, in seconds, displayed/uploaded alongside the current value
constexpr float AVERAGE_WINDOWS_S[NUM_WINDOWS] = { 10, 60, 120, 300, 600 };
constexpr const char* AVERAGE_WINDOW_LABELS[NUM_WINDOWS] = { "10s", "1m", "2m", "5m", "10m" };

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

TimedScatterPlot* activePlot = nullptr;
TimedScatterPlotSeries* activePlotSeries[NUM_SENSORS] = { nullptr };
int8_t activePlotView = -1;
ViewMode viewMode = ViewMode::TABLE;

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

Rect16 plotRect;

///
/// <summary>
/// Destroys the currently active plot view (if any), freeing its series/bin buffers.
/// </summary>
/// <returns>None</returns>
///
void deactivatePlotView()
{
   if (activePlot == nullptr)
   {
      return;
   }

   delete activePlot;
   activePlot = nullptr;
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      activePlotSeries[i] = nullptr;
   }
   activePlotView = -1;
}

///
/// <summary>
/// Creates the plot view for the given plot index (0 = Now, 1..NUM_WINDOWS = averaging
/// windows), replacing any previously active plot view.
/// </summary>
/// <param name="plotView">Zero-based plot view index (see ViewMode)</param>
/// <returns>None</returns>
///
void activatePlotView(uint8_t plotView)
{
   if (activePlotView == plotView)
   {
      return;
   }

   deactivatePlotView();

   unsigned long plotHistoryMs = 120*1000;
   activePlot = new TimedScatterPlot(&arduino, plotRect, plotHistoryMs, tempFormat, 0.0f);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      TimedScatterPlotSeries* series = activePlot->createSeries(0);
      series->showPoints = false;
      series->showLines = true;
      series->color = SENSOR_PLOT_COLORS[i];
      activePlotSeries[i] = series;

      // Seed the series with its current value right away so the plot shows data
      // immediately when switching views, rather than staying empty until the next
      // sensorTimer.ready() reading.
      if (sensors[i]->exists())
      {
         float plotValue = (plotView == 0) ? currentFields[i]->get() : averageFields[i][plotView - 1]->get();
         series->add(plotValue);
      }
   }
   activePlotView = plotView;
}

///
/// <summary>
/// Posts the current-value and time-averaged Influx points for every detected sensor.
/// </summary>
/// <returns>True if all points posted successfully</returns>
///
bool uploadAllPoints()
{
   bool allSucceeded = true;

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (!sensors[i]->exists())
      {
         continue;
      }

      if (!currentPoints[i]->post(&client))
      {
         allSucceeded = false;
      }

      for (uint8_t w = 0; w < NUM_WINDOWS; w++)
      {
         if (!averagePoints[i][w]->post(&client))
         {
            allSucceeded = false;
         }
      }
   }

   // All points above were only queued into the write buffer (see INFLUX_BATCH_SIZE), so
   // flush now to post everything in a single HTTP request sharing one write timestamp.
   if (!client.flushBuffer())
   {
      allSucceeded = false;
   }

   return allSucceeded;
}

void setup()
{
   SerialX::begin();
   Util::checkHaltReason();
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

   client.setWriteOptions(WriteOptions().batchSize(INFLUX_BATCH_SIZE).bufferSize(2 * INFLUX_BATCH_SIZE));

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
   uploadStatusField = new DisplayField(&arduino, uploadX, uploadY, "", uploadStatusFormat, 2, Color::GRAY, Color::GRAY);
   uploadStatusField->draw("");

   int16_t plotTop = arduino.charH() * 2;
   int16_t plotHeight = arduino.height() - plotTop;
   plotRect = { 0, static_cast<uint16_t>(plotTop), arduino.width(), static_cast<uint16_t>(plotHeight) };
}

void loop()
{
   const bool showType = arduino.buttonA.isPressed();
   static bool lastShowType = showType;

   int32_t viewDelta = arduino.encoderA.delta();
   if (viewDelta != 0)
   {
      int8_t newView = (static_cast<int8_t>(viewMode) + static_cast<int8_t>(viewDelta)) % NUM_VIEWS;
      if (newView < 0)
      {
         newView += NUM_VIEWS;
      }
      viewMode = static_cast<ViewMode>(newView);

      if (viewMode == ViewMode::TABLE)
      {
         deactivatePlotView();
      }
      else
      {
         activatePlotView(static_cast<uint8_t>(viewMode) - 1);
      }

      arduino.clearDisplay();
   }

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

         if (activePlot != nullptr)
         {
            float plotValue = (activePlotView == 0) ? tempF : averageFields[i][activePlotView - 1]->get();
            activePlotSeries[i]->add(plotValue);
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
   arduino.print("Mutli-Temp Monitor", Color::HEADING);
   if (viewMode != ViewMode::TABLE)
   {
      const char* rangeLabel = (activePlotView == 0) ? "Now" : AVERAGE_WINDOW_LABELS[activePlotView - 1];
      arduino.print(" - ", Color::HEADING);
      arduino.print(rangeLabel, Color::HEADING);
   }
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
   else if (viewMode == ViewMode::TABLE)
   {
      static Format idFormat("#", Format::Alignment::RIGHT);
      static const Color idColor = Color::WHITE;
      static const DisplayGrid::Column columns[] = {
         { "", &idFormat, &idColor },
         { "Now", &tempFormat },
         { AVERAGE_WINDOW_LABELS[0], &tempFormat },
         { AVERAGE_WINDOW_LABELS[1], &tempFormat },
         { AVERAGE_WINDOW_LABELS[2], &tempFormat },
         { AVERAGE_WINDOW_LABELS[3], &tempFormat },
         { AVERAGE_WINDOW_LABELS[4], &tempFormat },
      };
      DisplayGrid grid(&arduino, nullptr, columns, 7, Color::WHITE);
      grid.printHeader();

      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (!sensors[i]->exists())
         {
            grid.printRow(Color::GRAY, i, "----", "----", "----", "----", "----", "----");
            continue;
         }

         grid.printRow(Color::VALUE, i, currentFields[i]->get(), averageFields[i][0]->get(), averageFields[i][1]->get(),
                       averageFields[i][2]->get(), averageFields[i][3]->get(), averageFields[i][4]->get());
      }
   }
   else if (activePlot != nullptr)
   {
      activePlot->render();
   }

   if (influxTimer.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      uploadStatusField->draw("Upload");

      bool writeFailed = !uploadAllPoints();

      if (writeFailed && client.getLastStatusCode() <= 0)
      {
         // A transport-level failure (no HTTP status at all) usually means the reused
         // connection went stale (e.g. the server closed an idle keep-alive). Force a
         // fresh connection and retry once before giving up.
         Serial.println("InfluxDB write failed at transport level, reconnecting and retrying...");
         client.validateConnection();
         writeFailed = !uploadAllPoints();
      }

      if (writeFailed)
      {
         Serial.println("InfluxDB write failed: ");
         Serial.println(client.getLastErrorMessage());
         Serial.print("   HTTP status: ");
         Serial.println(client.getLastStatusCode());
         Serial.print("   WiFi status: ");
         Serial.println(WiFiX::statusString());
         Serial.print("   Free heap: ");
         Serial.println(ESP.getFreeHeap());
      }

                   digitalWrite(BUILTIN_LED, LOW);
                   uploadStatusField->draw("");
                }
             }

