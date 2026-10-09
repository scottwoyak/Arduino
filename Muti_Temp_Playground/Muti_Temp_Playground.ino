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
/// all tagged with the sensor's location and an "item" tag identifying which value they carry, each
/// posting a single "temperature" field. On each upload interval, all active sensor points are
/// posted individually. Wi-Fi connectivity is monitored continuously and triggers reset on loss.
/// 
/// Typical usage: start the sketch and verify detected sensors in Serial startup output, let averages
/// settle (up to 10 minutes for the longest window) before using displayed values for decisions,
/// then monitor the Influx stream for per-location current/averaged temperature values.
/// </remarks>

#include <array>

#include "ArduinoBoard.h"
#include "TempSensor.h"
#include "SerialX.h"
#include "I2CMultiplexor.h"
#include "Timer.h"
#include "Table.h"
#include "Field.h"
#include "Util.h"
#include "LibraryVersion.h"

#include "WiFiSettings.h"
#include "ScatterPlot.h"

#include "MonitorSketch.h"

Format tempFormat(" ##.###");
Format uploadStatusFormat(7, Format::Alignment::RIGHT);

constexpr uint8_t NUM_SENSORS = 8;
constexpr uint8_t NUM_WINDOWS = 5;

// Views cycled via encoderA: the temperature table, then one scatterplot per
// sampling rate (current value plus each averaging window). Only one plot view's
// ScatterPlot/series are ever allocated at a time (see _activatePlotView()),
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
   Color::YELLOW, Color::CYAN, Color::LIME, Color::MAGENTA,
   Color::ORANGE, Color::RED, Color::WHITE, Color::PINK,
};

constexpr uint8_t INFLUX_TEMP_DECIMAL_PLACES = 3;
constexpr uint8_t SENSOR_CORRECTION_DECIMAL_PLACES = 3;
constexpr uint16_t SENSOR_READ_INTERVAL_MS = 500;
constexpr auto INFLUX_MEASUREMENT = "Sensors";
constexpr auto INFLUX_INTERVAL_S = 10;
constexpr auto SKETCH_NAME = "Muti_Temp_Playground";
const auto VERSION = MakeVersion("1.0");
constexpr auto INFLUX_TEMPERATURE_FIELD_NAME = "temperature";
constexpr auto INFLUX_ITEM_TAG_NAME = "item";
constexpr auto INFLUX_STAT_CURRENT = "current";

// Averaging windows, in seconds, displayed/uploaded alongside the current value
constexpr float AVERAGE_WINDOWS_S[NUM_WINDOWS] = { 10, 60, 120, 300, 600 };
constexpr const char* AVERAGE_WINDOW_LABELS[NUM_WINDOWS] = { "10s", "1m", "2m", "5m", "10m" };

InfluxConfig INFLUX_CONFIG = {
   .context = { INFLUXDB_BUCKET_7_DAY, "Test", "Multi-Temp" },
   .measurement = INFLUX_MEASUREMENT,
};

SketchConfig MONITOR_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .enableOTA = true,
};

MonitorSketch sketch(MONITOR_CONFIG, INFLUX_CONFIG);
I2CMultiplexor multi;
Timer sensorTimer(SENSOR_READ_INTERVAL_MS);
TimerSecs influxTimer(INFLUX_INTERVAL_S);

TempSensor* sensors[NUM_SENSORS];
InfluxPoint* currentPoints[NUM_SENSORS];
InfluxField* currentFields[NUM_SENSORS];
InfluxPoint* averagePoints[NUM_SENSORS][NUM_WINDOWS];
InfluxField* averageFields[NUM_SENSORS][NUM_WINDOWS];
Field* uploadStatusField = nullptr;

ScatterPlot* activePlot = nullptr;
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

std::array tableColumns = {
   Table::Column(""),
   Table::Column("Now", tempFormat.formatString().c_str(), Table::Alignment::RIGHT),
   Table::Column(AVERAGE_WINDOW_LABELS[0], tempFormat.formatString().c_str(), Table::Alignment::RIGHT),
   Table::Column(AVERAGE_WINDOW_LABELS[1], tempFormat.formatString().c_str(), Table::Alignment::RIGHT),
   Table::Column(AVERAGE_WINDOW_LABELS[2], tempFormat.formatString().c_str(), Table::Alignment::RIGHT),
   Table::Column(AVERAGE_WINDOW_LABELS[3], tempFormat.formatString().c_str(), Table::Alignment::RIGHT),
   Table::Column(AVERAGE_WINDOW_LABELS[4], tempFormat.formatString().c_str(), Table::Alignment::RIGHT),
};
Table sensorTable(&sketch.arduino, 0, 0, tableColumns);
bool sensorTableBuilt = false;

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
   activePlot = new ScatterPlot(&sketch.arduino, plotRect, "-######", tempFormat.formatString());
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      TimedScatterPlotSeries* series = activePlot->createTimedSeries(plotHistoryMs, plotRect.width);
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

void setup()
{
   Wire.begin();

   sketch.begin();

   sketch.arduino.setTextSize(2);
   sketch.arduino.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   sketch.arduino.print("Sensors... ", Color::LABEL);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensors[i] = new TempSensor();

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
            // The sketch owns the "location" tag, so each sensor's name goes in "probe".
            currentPoints[i] = sketch.addPoint({ { "probe", locations[i] }, { INFLUX_ITEM_TAG_NAME, INFLUX_STAT_CURRENT } });
            currentFields[i] = currentPoints[i]->addValueField(INFLUX_TEMPERATURE_FIELD_NAME, INFLUX_TEMP_DECIMAL_PLACES);
            for (uint8_t w = 0; w < NUM_WINDOWS; w++)
            {
               averagePoints[i][w] = sketch.addPoint({ { "probe", locations[i] }, { INFLUX_ITEM_TAG_NAME, AVERAGE_WINDOW_LABELS[w] } });
               averageFields[i][w] = averagePoints[i][w]->addTimeAverageField(AVERAGE_WINDOWS_S[w], INFLUX_TEMPERATURE_FIELD_NAME, INFLUX_TEMP_DECIMAL_PLACES);
            }

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
         Serial.println("FAILED");
         sketch.reportSensorFailure();
      }
   }
   sketch.arduino.printlnR("ok", Color::VALUE);

   sketch.completeInitialization();

   sketch.arduino.setTextSize(2);
   std::string uploadSample(uploadStatusFormat.length(), '0');
   int16_t uploadX = sketch.arduino.width() - sketch.arduino.textWidth(uploadSample);
   int16_t uploadY = sketch.arduino.height() - sketch.arduino.charH();
   Point16 uploadPos(uploadX, uploadY);
   uploadStatusField = new Field(&sketch.arduino, uploadPos, uploadStatusFormat, 2);
   uploadStatusField->draw("", Color::LABEL, Color::GRAY);

   int16_t plotTop = sketch.arduino.charH() * 2;
   int16_t plotHeight = sketch.arduino.height() - plotTop;
   plotRect = { 0, static_cast<uint16_t>(plotTop), sketch.arduino.width(), static_cast<uint16_t>(plotHeight) };
}

void loop()
{
   const bool showType = sketch.arduino.buttonA.isPressed();
   static bool lastShowType = showType;

   int32_t viewDelta = sketch.arduino.encoderA.delta();
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

      sketch.arduino.clearDisplay();
   }

   if (showType != lastShowType)
   {
      sketch.arduino.clearDisplay();
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


   sketch.loop();

   sketch.arduino.setCursor(0, 0);
   sketch.arduino.setTextSize(3);
   sketch.arduino.print("Mutli-Temp Monitor", Color::HEADING);
   if (viewMode != ViewMode::TABLE)
   {
      const char* rangeLabel = (activePlotView == 0) ? "Now" : AVERAGE_WINDOW_LABELS[activePlotView - 1];
      sketch.arduino.print(" - ", Color::HEADING);
      sketch.arduino.print(rangeLabel, Color::HEADING);
   }
   sketch.arduino.println();
   sketch.arduino.setTextSize(2);
   sketch.arduino.moveCursorY(sketch.arduino.charH() / 3);

   if (showType)
   {
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         sketch.arduino.print(i, Color::GRAY);
         sketch.arduino.print(" ");

         if (!sensors[i]->exists())
         {
            sketch.arduino.println("----", Color::GRAY);
            continue;
         }

         sketch.arduino.println(sensors[i]->type(), Color::VALUE);
      }
   }
   else if (viewMode == ViewMode::TABLE)
   {
      if (!sensorTableBuilt)
      {
         sensorTable.setPosition(sketch.arduino.getCursorX(), sketch.arduino.getCursorY());
         for (uint8_t i = 0; i < NUM_SENSORS; i++)
         {
            char label[4];
            snprintf(label, sizeof(label), "%u", i);
            sensorTable.addRow(label, Color::VALUE);
         }
         sensorTableBuilt = true;
      }

      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (!sensors[i]->exists())
         {
            for (uint8_t c = 0; c < tableColumns.size(); c++)
            {
               sensorTable.setValueNone(i, c, Color::GRAY, '-');
            }
            continue;
         }

         sensorTable.setValue(i, 0, currentFields[i]->get(), Color::VALUE);
         sensorTable.setValue(i, 1, averageFields[i][0]->get(), Color::VALUE);
         sensorTable.setValue(i, 2, averageFields[i][1]->get(), Color::VALUE);
         sensorTable.setValue(i, 3, averageFields[i][2]->get(), Color::VALUE);
         sensorTable.setValue(i, 4, averageFields[i][3]->get(), Color::VALUE);
         sensorTable.setValue(i, 5, averageFields[i][4]->get(), Color::VALUE);
      }

      sensorTable.draw();
   }
   else if (activePlot != nullptr)
   {
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (activePlotSeries[i] != nullptr)
         {
            activePlotSeries[i]->updateWindow(millis());
         }
      }
      activePlot->draw();
   }

   if (influxTimer.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      uploadStatusField->draw("Upload", Color::LABEL, Color::GRAY);

      sketch.postPoints();

      digitalWrite(BUILTIN_LED, LOW);
      uploadStatusField->clear(Color::GRAY);
   }
}
