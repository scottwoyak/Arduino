//
// Profiles how fast a ScatterPlot can be redrawn as its backing sample series grows.
//
// Continuously samples a mock data source (see DATA_SOURCE_TYPE below) and appends each reading
// to a ScatterPlotSeries, calling
// ScatterPlot::draw() after every 10th new sample. draw() recomputes the shared axis range
// via ScatterPlotSeries::getRawRange(), which is maintained incrementally in O(1) by add(), so
// the per-update cost stays roughly flat as the series grows rather than scaling with sample
// count. The test stops once the rolling update rate drops to STOP_RATE_PER_SEC, or once the
// sample buffer safety cap is reached, whichever comes first.
//
// Serial output prints a final summary (including which stop condition was hit) once the test
// completes. The display shows a title, a status table on the left (source and live rate) with
// the source editable live via Encoder A (select) and Encoder B (change) even while sampling is
// in progress, and the scatter plot to the right of the table.
//

// System/standard library headers
#include <math.h>
#include <Wire.h>

// Local library headers (from libraries/Woyak)
#include "ESP32_S3_Playground.h"
#include "ValueEditor.h"
#include "FieldTableEditor.h"
#include "RollingRate.h"
#include "ScatterPlot.h"
#include "SerialX.h"
#include "TestSensor.h"
#include "Timer.h"
#include "Util.h"

// ----------- Test Function Selection
// The available mock test functions the user can select from at startup via a FieldTableEditor.
constexpr const char* TEST_FUNCTION_LABELS[] = { "Const", "Random", "Normal", "Sin" };
constexpr size_t NUM_TEST_FUNCTIONS = sizeof(TEST_FUNCTION_LABELS) / sizeof(TEST_FUNCTION_LABELS[0]);
constexpr const char* PREF_NAMESPACE = "ScatterPlotPg";

// ----------- Plot Size Selection
// The percentage of the available plot area (to the right of the status table) used for
// each axis. The plot is centered within that available area.
constexpr uint8_t PLOT_SIZE_PERCENTS[] = { 100,80, 60, 40,20 };
constexpr size_t NUM_PLOT_SIZES = sizeof(PLOT_SIZE_PERCENTS) / sizeof(PLOT_SIZE_PERCENTS[0]);

// ----------- Test Parameters
constexpr uint16_t RATE_WINDOW_SAMPLES = 10;
constexpr unsigned long STATUS_DRAW_INTERVAL_MS = 200; // throttles statusTable.draw() during sampling

// ----------- Sample Count Selection
// Rolling plot capacity, selectable at runtime; X axis spans [1, selected value], scrolling
// once full. See maxSamplesField below.
constexpr long MIN_MAX_SAMPLES = 100;
constexpr long MAX_MAX_SAMPLES = 10000;
constexpr long DEFAULT_MAX_SAMPLES = 1000;

// ----------- Display Geometry
// Matches the ESP32_S3_Playground board's LGX_Hosyond_ST7796 display in landscape orientation.
constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint16_t DISPLAY_HEIGHT = 320;
constexpr uint16_t HEADER_HEIGHT = 3 * 8 + 4; // title (size 3) plus padding
constexpr uint16_t TABLE_PLOT_GAP = 10; // gap between the status table and the scatter plot

// A darker gray than Color::DARKGRAY, used for the entire plot background (both the outer
// area and the plot area itself) so the chart reads as one solid dark panel. Useful for testing
//const Color PLOT_BACKGROUND_COLOR = Color565::fromRGB(70, 70, 70);
const Color PLOT_BACKGROUND_COLOR = Color::BLACK;

// ----------- The Board
ESP32_S3_Playground arduino;
ConstantTestSensor constantSensor;
RandomTestSensor randomSensor;
NormalTestSensor normalSensor;
SinTestSensor sinSensor(SinTestSensor::TIME_SOURCE_FIXED_STEP);
ITestSensor* const TEST_FUNCTION_SENSORS[] = { &constantSensor, &randomSensor, &normalSensor, &sinSensor };
ITestSensor* sensor = nullptr;

// ----------- Display Formats
EnumEditor testFunctionEditor(TEST_FUNCTION_LABELS, 0, "######");
FloatValue rateValueField("####/s");

uint32_t startFreeHeapBytes = 0;
FloatValue memoryValue("###.# kb");

///
/// <summary>
/// Plot-size-selection field controlling both the X and Y size percentages together (the
/// plot area is always resized uniformly on both axes). Steps through PLOT_SIZE_PERCENTS
/// by index (like EnumEditor), but formats its label directly from the selected
/// percentage instead of a separate parallel string-label array.
/// </summary>
///
class PlotSizeField : public IntEditor
{
public:
   PlotSizeField(const char* formatStr)
      : IntEditor(0, (long)NUM_PLOT_SIZES - 1, 1, 0, formatStr)
   {
   }

   void adjust(int32_t direction) override
   {
      long newValue = (get() + (direction > 0 ? 1 : -1) + (long)NUM_PLOT_SIZES) % (long)NUM_PLOT_SIZES;
      set(newValue);
   }

   std::string valueText() override
   {
      long index = constrain(get(), 0L, (long)(NUM_PLOT_SIZES - 1));
      return _format.toString((double)PLOT_SIZE_PERCENTS[index]);
   }
};

PlotSizeField plotSizeField("###%    ");

///
/// <summary>
/// Rolling-sample-count-selection field, spanning MIN_MAX_SAMPLES to MAX_MAX_SAMPLES with a
/// magnitude-scaled step size (see ScaledStepIntEditor) so coarse changes near 10000 don't
/// require an impractical number of encoder clicks, while fine changes remain available near
/// the low end of the range.
/// </summary>
///
ScaledStepIntEditor maxSamplesField(MIN_MAX_SAMPLES, MAX_MAX_SAMPLES, DEFAULT_MAX_SAMPLES, "#####   ");

// ----------- Series Display Mode Selection
// Controls whether the active sample series is drawn as raw points or connected lines.
// Applied to sampleSeries in applyDisplayMode() below, called whenever the field changes
// and once from recreatePlot().
constexpr const char* DISPLAY_MODE_LABELS[] = { "Points", "Lines" };
constexpr size_t NUM_DISPLAY_MODES = sizeof(DISPLAY_MODE_LABELS) / sizeof(DISPLAY_MODE_LABELS[0]);

///
/// <summary>
/// Series-display-mode-selection field. Switching this field's value changes whether points
/// or connected lines are drawn for the active sample series; see applyDisplayMode().
/// </summary>
///
EnumEditor displayModeEditor(
   DISPLAY_MODE_LABELS, 0, "########");

// ----------- Point Size Selection
// Controls the pixel size of each drawn point (see IScatterPlotSeries::pointSize). Only
// meaningful while Display is set to Points; grayed out and skipped by encoder selection
// otherwise, since it has no effect while lines are drawn instead of points.
constexpr long MIN_POINT_SIZE = 1;
constexpr long MAX_POINT_SIZE = 3;
constexpr long DEFAULT_POINT_SIZE = 1;

///
/// <summary>
/// Point-size-selection field that is only enabled (selectable/adjustable, drawn in its
/// normal color) while Display is set to Points. While Display is Lines, it's grayed out
/// and skipped by encoder selection since it has no effect on the drawn series. Adjusting
/// wraps around at either end (e.g. incrementing past the max wraps to the min) instead of
/// clamping.
/// </summary>
///
class PointSizeField : public IntEditor
{
public:
   using IntEditor::IntEditor;

   bool isEnabled() const override
   {
      return displayModeEditor.get() == 0;
   }

   void adjust(int32_t direction) override
   {
      long count = _maxValue - _minValue + 1;
      set(_minValue + (((get() - _minValue) + (direction > 0 ? 1 : -1) + count) % count));
   }
};

PointSizeField pointSizeField(
   MIN_POINT_SIZE, MAX_POINT_SIZE, 1, DEFAULT_POINT_SIZE, "########");

// ----------- Stats Overlay Selection
// Controls which statistical overlays (moving average, moving stddev band) are drawn on top
// of the active sample series. Applied to sampleSeries in applyStatsMode() below, called
// whenever the field changes and once from recreatePlot().
constexpr const char* STATS_MODE_LABELS[] = { "None", "Avg", "StdDev", "Both" };
constexpr size_t NUM_STATS_MODES = sizeof(STATS_MODE_LABELS) / sizeof(STATS_MODE_LABELS[0]);

///
/// <summary>
/// Stats-overlay-selection field. Switching this field's value changes which of the moving
/// average/moving stddev band overlays are drawn for the active sample series; see
/// applyStatsMode().
/// </summary>
///
EnumEditor statsModeEditor(
   STATS_MODE_LABELS, 0, "########");

// ----------- Redraw Method Selection
// Controls how ScatterPlot's shared DisplayBuffer repaints each frame; see
// DisplayBuffer::RedrawMode and ScatterPlot::setRedrawMode(). Applied in
// applyRedrawMode() below, called whenever the field changes and once from recreatePlot().
constexpr const char* REDRAW_MODE_LABELS[] = { "Full", "Diff" };
constexpr size_t NUM_REDRAW_MODES = sizeof(REDRAW_MODE_LABELS) / sizeof(REDRAW_MODE_LABELS[0]);

///
/// <summary>
/// Redraw-method-selection field. Switching this field's value changes how the plot's
/// shared DisplayBuffer repaints each frame - Full always repaints every pixel; Diff only
/// repaints pixels that changed since the previous frame. Both use bulk row/column
/// transfers. See applyRedrawMode().
/// </summary>
///
EnumEditor redrawModeEditor(
   REDRAW_MODE_LABELS, 1, "#########");

// ----------- Source-Specific Configuration Fields
// The Constant source lets the user set its value directly; the Sin source lets the user
// adjust its period (always fixed-step; see sinPeriodField). Each source's field array is
// swapped into statusTable by applyTestFunction() below.
FloatEditor constantValueEditor(
   TestSensorConfig::CONSTANT_MIN_VALUE, TestSensorConfig::CONSTANT_MAX_VALUE,
   TestSensorConfig::CONSTANT_STEP, TestSensorConfig::CONSTANT_VALUE, "####.#");

///
/// <summary>
/// Sin period field that displays whole-number values as a plain sample count with no unit,
/// since the period doesn't correspond to real elapsed time - it advances by a fixed step per
/// sample (see SinTestSensor::TIME_SOURCE_FIXED_STEP, the sensor's only sampling mode).
/// </summary>
///
class SinPeriodField : public FloatEditor
{
public:
   using FloatEditor::FloatEditor;

   void adjust(int32_t direction) override
   {
      long samples = lroundf(get() / TestSensorConfig::SIN_FIXED_STEP_S);
      samples += direction * TestSensorConfig::SIN_FIXED_PERIOD_STEP_SAMPLES;
      samples = constrain(samples, TestSensorConfig::SIN_FIXED_MIN_PERIOD_SAMPLES, TestSensorConfig::SIN_FIXED_MAX_PERIOD_SAMPLES);
      set(samples * TestSensorConfig::SIN_FIXED_STEP_S);
   }

   std::string valueText() override
   {
      long samples = lroundf(get() / TestSensorConfig::SIN_FIXED_STEP_S);
      return _format.toString(String(samples));
   }
};

SinPeriodField sinPeriodField(
   TestSensorConfig::SIN_MIN_PERIOD_S, TestSensorConfig::SIN_MAX_PERIOD_S,
   TestSensorConfig::SIN_PERIOD_STEP_S, TestSensorConfig::SIN_PERIOD_S, "########");

// ----------- Noise Configuration Fields
// Shared by every mock test function via MockTestSensorBase::noiseStdDev. The Noise field
// toggles noise on/off (true/false); the StdDev field is only meaningful while Noise is true,
// but stays visible so the user can pre-configure it before enabling noise.

///
/// <summary>
/// Noise on/off field. When toggled, applyNoise() (called from loop()) pushes noiseStdDev
/// onto the active sensor: the configured StdDev value when true, or 0 when false.
/// </summary>
///
constexpr const char* NOISE_ENABLED_LABELS[] = { "False", "True" };
constexpr size_t NUM_NOISE_ENABLED_STATES = sizeof(NOISE_ENABLED_LABELS) / sizeof(NOISE_ENABLED_LABELS[0]);

EnumEditor noiseEnabledEditor(
   NOISE_ENABLED_LABELS, 0, "########");

///
/// <summary>
/// Noise standard-deviation field that is only enabled (selectable/adjustable, drawn in its
/// normal color) while Noise is True. While Noise is False, it's grayed out and skipped by
/// encoder selection since it has no effect on the active sensor.
/// </summary>
///
class NoiseStdDevField : public FloatEditor
{
public:
   using FloatEditor::FloatEditor;

   bool isEnabled() const override
   {
      return noiseEnabledEditor.get() != 0;
   }
};

NoiseStdDevField noiseStdDevField(
   TestSensorConfig::NOISE_MIN_STDDEV, TestSensorConfig::NOISE_MAX_STDDEV,
   TestSensorConfig::NOISE_STDDEV_STEP, TestSensorConfig::NOISE_STDDEV, "####.#");

// ----------- Section Headers
// The Test Function section is shared by the always-present Source row, the source-specific
// configuration rows, and the Noise/StdDev rows; the Plot section starts at the plot-size rows;
// the Measured section starts at the FPS row. Section headers are their own label-only
// FieldTableEditor::Row entries (no value), rendered above the rows that follow them.

FieldTableEditor::Row defaultStatusCells[] =
{
   { "Test Function" },
   { "Source", &testFunctionEditor },
   { "Noise", &noiseEnabledEditor },
   { "StdDev", &noiseStdDevField },
   { "Plot" },
   { "Size", &plotSizeField },
   { "Samples", &maxSamplesField },
   { "Display", &displayModeEditor },
   { "Point Size", &pointSizeField },
   { "Stats", &statsModeEditor },
   { "Redraw", &redrawModeEditor },
   { "Measured" },
   { "FPS", &rateValueField },
   { "Memory", &memoryValue },
};
FieldTableEditor::Row constantStatusCells[]
{
   { "Test Function" },
   { "Source", &testFunctionEditor },
   { "Value", &constantValueEditor },
   { "Noise", &noiseEnabledEditor },
   { "StdDev", &noiseStdDevField },
   { "Plot" },
   { "Size", &plotSizeField },
   { "Samples", &maxSamplesField },
   { "Display", &displayModeEditor },
   { "Point Size", &pointSizeField },
   { "Stats", &statsModeEditor },
   { "Redraw", &redrawModeEditor },
   { "Measured" },
   { "FPS", &rateValueField },
   { "Memory", &memoryValue },
};
FieldTableEditor::Row sinStatusCells[]
{
   { "Test Function" },
   { "Source", &testFunctionEditor },
   { "Period", &sinPeriodField },
   { "Noise", &noiseEnabledEditor },
   { "StdDev", &noiseStdDevField },
   { "Plot" },
   { "Size", &plotSizeField },
   { "Samples", &maxSamplesField },
   { "Display", &displayModeEditor },
   { "Point Size", &pointSizeField },
   { "Stats", &statsModeEditor },
   { "Redraw", &redrawModeEditor },
   { "Measured" },
   { "FPS", &rateValueField },
   { "Memory", &memoryValue },
};
FieldTableEditor statusTable(&arduino, PREF_NAMESPACE, defaultStatusCells,
   0, HEADER_HEIGHT, 2);

// ----------- Test State
RollingRate updateRate(RATE_WINDOW_SAMPLES);
// Allocated in setup(), after startFreeHeapBytes is recorded, so the plot's own memory usage
// is included in the measured memory delta. Held through the shared ScatterPlot/
// IScatterPlotSeries interface, matching recreatePlot()'s single rolling-count series setup.
ScatterPlot* scatterPlot = nullptr;
IScatterPlotSeries* sampleSeries = nullptr;

size_t sampleCount = 0;
bool sensorReady = false;

// Throttles statusTable.draw() (called from updateRateReadout() during sampling) to every
// STATUS_DRAW_INTERVAL_MS, since redrawing the whole table on every draw() is unrelated
// per-call overhead that would otherwise scale with the sample rate.
TimerMillis statusDrawTimer(STATUS_DRAW_INTERVAL_MS);

///
/// <summary>
/// Clears the display and draws the sketch title.
/// </summary>
///
void drawTitle()
{
   arduino.setTextSize(3);
   arduino.clearDisplay();
   arduino.println("Scatter Plot Playground", Color::HEADING);
}

///
/// <summary>
/// Gets the combined free heap across both internal SRAM and external PSRAM. On ESP32-S3
/// boards with PSRAM, ESP.getFreeHeap() alone only reports free internal heap - once an
/// allocation grows large enough to be satisfied from PSRAM instead of internal SRAM, that
/// allocation disappears from ESP.getFreeHeap()'s accounting entirely, making memory usage
/// look like it *decreased* even though it actually grew. Combining both regions gives a
/// true total memory-used reading regardless of which region backs a given allocation.
/// </summary>
/// <returns>Total free heap, in bytes, across internal SRAM and PSRAM.</returns>
///
uint32_t getTotalFreeHeap()
{
   return ESP.getFreeHeap() + ESP.getFreePsram();
}

///
/// <summary>
/// Recomputes memoryValue from the current free heap relative to startFreeHeapBytes.
/// Called immediately after any plot recreation (so the Memory row reflects the new
/// plot's allocation right away) as well as periodically from updateRateReadout().
/// </summary>
///
void updateMemoryReadout()
{
   memoryValue.set((float)((int32_t)startFreeHeapBytes - (int32_t)getTotalFreeHeap()) / 1024.0f);
}

///
/// <summary>
/// Updates the live update-rate row in the status table, redrawing it at most every
/// STATUS_DRAW_INTERVAL_MS (see statusDrawTimer) so the redraw itself doesn't add overhead
/// on every sample-driven draw() call.
/// </summary>
///
void updateRateReadout()
{
   rateValueField.set(updateRate.get());
   updateMemoryReadout();

   if (statusDrawTimer.ready())
   {
      statusTable.draw();
   }
}

///
/// <summary>
/// Computes the scatter plot's rectangle
/// and the status table's current width. The plot area (to the right of the status table)
/// is only ever partially filled per the selected percentages; the resulting rectangle is
/// centered within that available area. The letterbox area surrounding the plot rect is only
/// physically repainted when the available area actually changed (e.g. table width or size
/// selection changed), so unrelated recreations (e.g. switching Source) don't redundantly
/// repaint the whole area on every call.
/// </summary>
/// <returns>The computed plot rectangle.</returns>
///
Rect16 computePlotRect()
{
   int16_t plotAreaX = statusTable.width() + TABLE_PLOT_GAP;
   int16_t availableWidth = DISPLAY_WIDTH - plotAreaX;
   int16_t availableHeight = DISPLAY_HEIGHT - HEADER_HEIGHT;

   uint8_t sizePercent = PLOT_SIZE_PERCENTS[constrain(plotSizeField.get(), 0L, (long)(NUM_PLOT_SIZES - 1))];

   int16_t plotWidth = (int16_t)((int32_t)availableWidth * sizePercent / 100);
   int16_t plotHeight = (int16_t)((int32_t)availableHeight * sizePercent / 100);

   int16_t plotX = plotAreaX + (availableWidth - plotWidth) / 2;
   int16_t plotY = HEADER_HEIGHT + (availableHeight - plotHeight) / 2;

   static int16_t lastPlotAreaX = -1;
   static int16_t lastAvailableWidth = -1;
   static int16_t lastAvailableHeight = -1;
   static uint8_t lastSizePercent = 0;
   bool availableAreaChanged = (plotAreaX != lastPlotAreaX) || (availableWidth != lastAvailableWidth) || (availableHeight != lastAvailableHeight)
      || (sizePercent != lastSizePercent);
   if (availableAreaChanged)
   {
      arduino.fillRect(plotAreaX, HEADER_HEIGHT, availableWidth, availableHeight, PLOT_BACKGROUND_COLOR);
      lastPlotAreaX = plotAreaX;
      lastAvailableWidth = availableWidth;
      lastAvailableHeight = availableHeight;
      lastSizePercent = sizePercent;
   }

   return Rect16{ (uint16_t)plotX, (uint16_t)plotY, (uint16_t)plotWidth, (uint16_t)plotHeight };
}

///
/// <summary>
/// (Re)creates the active plot at its current rectangle (see computePlotRect()), using a
/// single rolling-count ScatterPlot series of the currently selected sample count (see
/// maxSamplesField/ScatterPlot::createRollingSeries()): points fill in from the left and,
/// once full, the oldest point scrolls off as each new one is added. Called whenever the plot
/// size or sample count changes, and once from setup().
/// </summary>
///
void recreatePlot()
{
   delete scatterPlot;

   Rect16 rect = computePlotRect();

   scatterPlot = new ScatterPlot(&arduino, rect, "#####", "##.#");

   size_t maxSamples = static_cast<size_t>(maxSamplesField.get());
   ScatterPlotSeries* rollingSeries = scatterPlot->createRollingSeries(maxSamples);
   rollingSeries->movingSampleSize = (float)maxSamples / 5.0f;
   sampleSeries = rollingSeries;

   scatterPlot->setColors(PLOT_BACKGROUND_COLOR, PLOT_BACKGROUND_COLOR, Color::GRAY, Color::LABEL);
   scatterPlot->setYAxisMode(ScatterPlot::AxisMode::GROW_ONLY);

   applyDisplayMode();
   applyStatsMode();
   applyRedrawMode();

   if (sensor != nullptr)
   {
      scatterPlot->setYAxisFormat(sensor->getFormatStr());
   }
}

///
/// <summary>
/// Applies the current Display field selection to the active sample series' display flags:
/// Points shows individual samples only; Lines connects samples with a line instead.
/// Called whenever the field changes and once from recreatePlot().
/// </summary>
///
void applyDisplayMode()
{
   long index = constrain(displayModeEditor.get(), 0L, (long)(NUM_DISPLAY_MODES - 1));

   sampleSeries->showPoints = (index == 0);
   sampleSeries->showLines = (index == 1);
   sampleSeries->pointSize = (uint8_t)constrain(pointSizeField.get(), MIN_POINT_SIZE, MAX_POINT_SIZE);
}

///
/// <summary>
/// Applies the current Stats field selection to the active sample series' overlay flags:
/// None shows no overlay; Avg adds a moving-average line; StdDev adds a moving stddev band;
/// All adds both. Called whenever the field changes and once from recreatePlot().
/// </summary>
///
void applyStatsMode()
{
   long index = constrain(statsModeEditor.get(), 0L, (long)(NUM_STATS_MODES - 1));

   sampleSeries->showMovingAverage = (index == 1 || index == 3);
   sampleSeries->showStdDevBand = (index == 2 || index == 3);
}

///
/// <summary>
/// Applies the current Redraw field selection to the active plot's shared DisplayBuffer
/// redraw mode - Full always repaints every pixel; Diff only repaints pixels that changed
/// since the previous frame using bulk column transfers. Called whenever the field changes
/// and once from recreatePlot().
/// </summary>
///
void applyRedrawMode()
{
   long index = constrain(redrawModeEditor.get(), 0L, (long)(NUM_REDRAW_MODES - 1));

   static constexpr DisplayBuffer::RedrawMode REDRAW_MODES[] =
   {
      DisplayBuffer::RedrawMode::FULL,
      DisplayBuffer::RedrawMode::DIFF,
   };

   scatterPlot->setRedrawMode(REDRAW_MODES[index]);
}


///
/// <summary>
/// Applies the current Noise/StdDev field settings to the active sensor: the configured
/// StdDev value when Noise is true, or 0 when Noise is false.
/// </summary>
///
void applyNoise()
{
   if (sensor == nullptr)
   {
      return;
   }

   sensor->setNoiseStdDev(noiseEnabledEditor.get() != 0 ? noiseStdDevField.get() : 0.0f);
}

///
/// <summary>
/// Selects the sensor for the current testFunctionEditor selection, begins it, applies noise, and swaps
/// the status table to the field set appropriate for that sensor (which may change the
/// table's width). Split out from applyTestFunction() so recreatePlot() can be called
/// afterward with the correct table width already in place.
/// </summary>
///
void selectTestFunction()
{
   sensor = TEST_FUNCTION_SENSORS[testFunctionEditor.get()];
   sensorReady = sensor->begin();
   applyNoise();

   if (sensor == &constantSensor)
   {
      constantValueEditor.set(constantSensor.value);
      statusTable.setFields(constantStatusCells);
   }
   else if (sensor == &sinSensor)
   {
      sinPeriodField.set(sinSensor.periodS);
      statusTable.setFields(sinStatusCells);
   }
   else
   {
      statusTable.setFields(defaultStatusCells);
   }
   statusTable.load();

   // statusTable.load() above may have just overwritten constantValueEditor/sinPeriodField
   // with the persisted value, but the sensors themselves still hold whatever value they
   // were constructed/last set with - normally kept in sync via loop()'s assignment on every
   // encoder change, which hasn't happened yet at startup. Push the freshly loaded editor
   // value into the sensor now so the very first sample reflects the restored setting.
   if (sensor == &constantSensor)
   {
      constantSensor.value = constantValueEditor.get();
   }
   else if (sensor == &sinSensor)
   {
      sinSensor.periodS = sinPeriodField.get();
   }
}

///
/// <summary>
/// Switches the active sensor to the currently selected test function and recreates the plot
/// (which also resets sample count, series data, and update rate) so the plot starts a fresh
/// run. Called both at startup and whenever the live test function field changes. Also swaps
/// in the source-specific configuration rows (e.g. Value for Constant, Sampling and Period
/// for Sin) alongside the always-present Source and Rate rows.
/// </summary>
///
void applyTestFunction()
{
   selectTestFunction();
   recreatePlot();

   sampleCount = 0;
   updateRate.reset();

   rateValueField.set(updateRate.get());
   updateMemoryReadout();
   arduino.setTextSize(2);
   statusTable.draw();

   if (!sensorReady)
   {
      arduino.setTextSize(2);
      arduino.setCursor(0, HEADER_HEIGHT);
      arduino.println("Sensor init failed", Color::RED);
      Serial.println("Error: sensor initialization failed");
      return;
   }
}

///
/// <summary>
/// Clears the currently collected plot data and restarts the test using the same sensor and
/// status table configuration, without resetting the sensor itself. Triggered by buttonB.
/// </summary>
///
void clearPlot()
{
   sampleSeries->clear();
   sampleCount = 0;
   updateRate.reset();

   scatterPlot->clear();

   rateValueField.set(updateRate.get());
   arduino.setTextSize(2);
   statusTable.draw();
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();
   statusTable.load();

   drawTitle();

   // Restore text size 2 (used by the status table and plot) immediately after drawing the
   // title at size 3, since computePlotRect() below measures the table's width via charW(),
   // which depends on the currently active text size.
   arduino.setTextSize(2);

   startFreeHeapBytes = getTotalFreeHeap();

   // applyTestFunction() selects the sensor and its field set (which determines the status
   // table's width) before recreating the plot, so computePlotRect() sizes the plot
   // correctly from the very first frame (e.g. a 100% width plot isn't sized against the
   // wrong table width at startup).
   applyTestFunction();

   // Discard any spurious position change accumulated on the encoders while pins were
   // settling during begin()/applyTestFunction(), so the first real turn moves the selection
   // immediately instead of just clearing a phantom delta.
   arduino.encoderA.reset();
   arduino.encoderB.reset();
}

void loop()
{
   if (arduino.buttonA.wasPressed())
   {
      Util::reset();
   }

   if (arduino.buttonB.wasPressed())
   {
      clearPlot();
   }

   int32_t statusSelectDelta = arduino.encoderA.delta();
   int32_t statusAdjustDelta = arduino.encoderB.delta();
   if (statusSelectDelta != 0 || statusAdjustDelta != 0)
   {
      statusTable.selectNext(statusSelectDelta);
      statusTable.adjustSelected(statusAdjustDelta);

      constantSensor.value = constantValueEditor.get();
      sinSensor.periodS = sinPeriodField.get();

      statusTable.save();
      arduino.setTextSize(2);

      // Call hasChanged() on every editor (not just short-circuited ones) so each editor's
      // internal baseline always stays current even when a different field is the one that
      // actually changed this tick.
      bool testFunctionChanged = testFunctionEditor.hasChanged();
      bool plotSizeChanged = plotSizeField.hasChanged();
      bool maxSamplesChanged = maxSamplesField.hasChanged();
      bool noiseEnabledChanged = noiseEnabledEditor.hasChanged();
      bool noiseStdDevChanged = noiseStdDevField.hasChanged();
      bool displayModeChanged = displayModeEditor.hasChanged();
      bool pointSizeChanged = pointSizeField.hasChanged();
      bool statsModeChanged = statsModeEditor.hasChanged();
      bool redrawModeChanged = redrawModeEditor.hasChanged();

      if (testFunctionChanged)
      {
         applyTestFunction();
      }
      else if (plotSizeChanged || maxSamplesChanged)
      {
         recreatePlot();
         sampleSeries->clear();
         sampleCount = 0;
         updateRate.reset();
         rateValueField.set(updateRate.get());
         updateMemoryReadout();
         statusTable.draw();
      }
      else if (noiseEnabledChanged || noiseStdDevChanged)
      {
         applyNoise();
         statusTable.draw();
      }
      else if (displayModeChanged || pointSizeChanged)
      {
         applyDisplayMode();
         scatterPlot->invalidate();
         scatterPlot->draw();
         statusTable.draw();
      }
      else if (statsModeChanged)
      {
         applyStatsMode();
         scatterPlot->invalidate();
         scatterPlot->draw();
         statusTable.draw();
      }
      else if (redrawModeChanged)
      {
         applyRedrawMode();
         statusTable.draw();
      }
      else
      {
         statusTable.draw();
      }
   }

   if (!sensorReady)
   {
      return;
   }

   float value = sensor->get();
   if (!isfinite(value))
   {
      return;
   }

   sampleSeries->add(value);
   sampleCount++;

   if ((sampleCount % 10) == 0)
   {
      updateRate.tick();
      scatterPlot->draw();
      updateRateReadout();
   }
}
