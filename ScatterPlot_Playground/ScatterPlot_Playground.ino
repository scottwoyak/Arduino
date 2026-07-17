//
// Profiles how fast a ScatterPlot can be redrawn as its backing sample series grows.
//
// Continuously samples a mock data source (see DATA_SOURCE_TYPE below) and appends each reading
// to a ScatterPlotSeries, calling
// ScatterPlot::render() after every 10th new sample. Because render() recomputes the shared
// axis range by scanning every point stored in the series on each call, the time per update
// grows as the series grows, so the achieved update rate falls over the course of the test.
// The test stops once the rolling update rate drops to STOP_RATE_PER_SEC, or once the sample
// buffer safety cap is reached, whichever comes first.
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
#include "DisplayEditableField.h"
#include "DisplayEditableTable.h"
#include "IScatterPlot.h"
#include "RollingRate.h"
#include "ScatterPlot.h"
#include "SerialX.h"
#include "TestSensor.h"
#include "Timer.h"
#include "TimedScatterPlot.h"
#include "Util.h"

// ----------- Test Function Selection
// The available mock test functions the user can select from at startup via a DisplayEditor.
constexpr const char* TEST_FUNCTION_LABELS[] = { "Const", "Random", "Normal", "Sin" };
constexpr size_t NUM_TEST_FUNCTIONS = sizeof(TEST_FUNCTION_LABELS) / sizeof(TEST_FUNCTION_LABELS[0]);
constexpr const char* PREF_NAMESPACE = "ScatterPlotPg";

// ----------- Plot Size Selection
// The percentage of the available plot area (to the right of the status table) used for
// each axis. The plot is centered within that available area.
constexpr uint8_t PLOT_SIZE_PERCENTS[] = { 100,80, 60, 40,20 };
constexpr size_t NUM_PLOT_SIZES = sizeof(PLOT_SIZE_PERCENTS) / sizeof(PLOT_SIZE_PERCENTS[0]);

// ----------- Plot Type Selection
// "Fixed" uses a ScatterPlot, which stops the test once the samples/rate stop conditions
// below are hit. "Timed" uses a TimedScatterPlot instead, which has no end point: data is
// continually generated and displayed within a rolling TIMED_HISTORY_S window until buttonB
// clears it or the source changes. Both are driven through the shared IScatterPlot/
// IScatterPlotSeries interface (see IScatterPlot.h) so the rest of the sketch doesn't need to
// know which concrete plot type is active.
constexpr const char* PLOT_TYPE_LABELS[] = { "Fixed", "Timed" };
constexpr size_t NUM_PLOT_TYPES = sizeof(PLOT_TYPE_LABELS) / sizeof(PLOT_TYPE_LABELS[0]);
constexpr unsigned long TIMED_HISTORY_S = 20;

// ----------- Test Parameters
constexpr float STOP_RATE_PER_SEC = 10.0f; // when the plot slows below this rate, stop collecting samples
constexpr uint16_t RATE_WINDOW_SAMPLES = 10;
constexpr size_t MIN_SAMPLES_BEFORE_STOP_CHECK = RATE_WINDOW_SAMPLES * 10;
constexpr size_t MAX_SAMPLES = 30000; // safety cap in case the rate never reaches STOP_RATE_PER_SEC
constexpr unsigned long STATUS_DRAW_INTERVAL_MS = 200; // throttles statusTable.draw() during sampling

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
Format rateFormat("####/s", Format::Alignment::LEFT);
Format memoryFormat("###.# kb");
Format xAxisFormat("#####");

Format testFunctionFormat(6);
long testFunctionIndex = 0;
long lastTestFunctionIndex = 0;
EnumDisplayEditableField testFunctionField("Source", &testFunctionIndex,
   TEST_FUNCTION_LABELS, NUM_TEST_FUNCTIONS, 0, testFunctionFormat);
float rateValue = 0.0f;
ReadOnlyDisplayEditableField rateField("Rate", &rateValue, rateFormat);

uint32_t startFreeHeapBytes = 0;
float memoryDeltaKb = 0.0f;
ReadOnlyDisplayEditableField memoryField("Memory", &memoryDeltaKb, memoryFormat);

///
/// <summary>
/// Plot-size-selection field shared by the X Size and Y Size rows. Steps through PLOT_SIZE_PERCENTS
/// by index (like EnumDisplayEditableField), but formats its label directly from the selected
/// percentage instead of a separate parallel string-label array.
/// </summary>
///
class PlotSizeField : public IntDisplayEditableField
{
public:
   PlotSizeField(const char* label, long* value, const Format& format)
      : IntDisplayEditableField(label, value, 0, (long)NUM_PLOT_SIZES - 1, 1, 0, format)
   {
   }

   void adjust(int32_t direction) override
   {
      long newValue = (*_value + (direction > 0 ? 1 : -1) + (long)NUM_PLOT_SIZES) % (long)NUM_PLOT_SIZES;
      *_value = newValue;
   }

   std::string valueText() override
   {
      long index = constrain(*_value, 0L, (long)(NUM_PLOT_SIZES - 1));
      return _format.toString((double)PLOT_SIZE_PERCENTS[index]);
   }
};

Format plotSizeFormat("###%", 8);
long plotXSizeIndex = 0;
long plotYSizeIndex = 0;
long lastPlotXSizeIndex = 0;
long lastPlotYSizeIndex = 0;
PlotSizeField plotXSizeField("X Size", &plotXSizeIndex, plotSizeFormat);
PlotSizeField plotYSizeField("Y Size", &plotYSizeIndex, plotSizeFormat);

///
/// <summary>
/// Plot-type-selection field. Switching this field's value swaps the active concrete plot
/// implementation (ScatterPlot vs TimedScatterPlot) behind the shared IScatterPlot pointer;
/// see applyPlotType().
/// </summary>
///
Format plotTypeFormat(8);
long plotTypeIndex = 0;
long lastPlotTypeIndex = 0;
EnumDisplayEditableField plotTypeField("Type", &plotTypeIndex,
   PLOT_TYPE_LABELS, NUM_PLOT_TYPES, 0, plotTypeFormat);

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
Format displayModeFormat(8);
long displayModeIndex = 0;
long lastDisplayModeIndex = 0;
EnumDisplayEditableField displayModeField("Display", &displayModeIndex,
   DISPLAY_MODE_LABELS, NUM_DISPLAY_MODES, 0, displayModeFormat);

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
Format statsModeFormat(8);
long statsModeIndex = 0;
long lastStatsModeIndex = 0;
EnumDisplayEditableField statsModeField("Stats", &statsModeIndex,
   STATS_MODE_LABELS, NUM_STATS_MODES, 0, statsModeFormat);

// ----------- Source-Specific Configuration Fields
// The Constant source lets the user set its value directly; the Sin source lets the user
// choose its time source (Clock vs Fixed) and adjust its period. Each source's field array is
// swapped into statusTable by applyTestFunction() below.
Format constantValueFormat("####.#");
FloatDisplayEditableField constantValueField("Value", &constantSensor.value,
   TestSensorConfig::CONSTANT_MIN_VALUE, TestSensorConfig::CONSTANT_MAX_VALUE,
   TestSensorConfig::CONSTANT_STEP, TestSensorConfig::CONSTANT_VALUE, constantValueFormat);

///
/// <summary>
/// Sin time-source-selection field. Labels are ordered to match SinTestSensor::TIME_SOURCE_CLOCK
/// (0) and SinTestSensor::TIME_SOURCE_FIXED_STEP (1).
/// </summary>
///
constexpr const char* SIN_TIME_SOURCE_LABELS[] = { "Clock", "Fixed" };
constexpr size_t NUM_SIN_TIME_SOURCES = sizeof(SIN_TIME_SOURCE_LABELS) / sizeof(SIN_TIME_SOURCE_LABELS[0]);

Format sinTimeSourceFormat(8);
EnumDisplayEditableField sinTimeSourceField("Sampling", &sinSensor.timeSource,
   SIN_TIME_SOURCE_LABELS, NUM_SIN_TIME_SOURCES, SinTestSensor::TIME_SOURCE_FIXED_STEP, sinTimeSourceFormat);

///
/// <summary>
/// Sin period field that displays whole-number values with a unit that depends on the
/// currently selected sampling mode. While Clock, the period is real wall-clock time, so it's
/// shown in seconds ("s"). While Fixed, the period doesn't correspond to real elapsed time -
/// it advances by a fixed step per sample - so it's shown as a plain sample count with no unit
/// instead.
/// </summary>
///
class SinPeriodField : public FloatDisplayEditableField
{
public:
   using FloatDisplayEditableField::FloatDisplayEditableField;

   void adjust(int32_t direction) override
   {
      if (sinSensor.timeSource == SinTestSensor::TIME_SOURCE_FIXED_STEP)
      {
         long samples = lroundf(*_value / TestSensorConfig::SIN_FIXED_STEP_S);
         samples += direction * TestSensorConfig::SIN_FIXED_PERIOD_STEP_SAMPLES;
         samples = constrain(samples, TestSensorConfig::SIN_FIXED_MIN_PERIOD_SAMPLES, TestSensorConfig::SIN_FIXED_MAX_PERIOD_SAMPLES);
         *_value = samples * TestSensorConfig::SIN_FIXED_STEP_S;
         return;
      }

      FloatDisplayEditableField::adjust(direction);
   }

   std::string valueText() override
   {
      if (sinSensor.timeSource == SinTestSensor::TIME_SOURCE_FIXED_STEP)
      {
         long samples = lroundf(*_value / TestSensorConfig::SIN_FIXED_STEP_S);
         return _format.toString(String(samples));
      }

      return _format.toString(String(lroundf(*_value)) + "s");
   }
};

Format sinPeriodFormat(8);
SinPeriodField sinPeriodField("Period", &sinSensor.periodS,
   TestSensorConfig::SIN_MIN_PERIOD_S, TestSensorConfig::SIN_MAX_PERIOD_S,
   TestSensorConfig::SIN_PERIOD_STEP_S, TestSensorConfig::SIN_PERIOD_S, sinPeriodFormat);

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

Format noiseEnabledFormat(8);
long noiseEnabled = 0;
long lastNoiseEnabled = 0;
EnumDisplayEditableField noiseEnabledField("Noise", &noiseEnabled,
   NOISE_ENABLED_LABELS, NUM_NOISE_ENABLED_STATES, 0, noiseEnabledFormat);

///
/// <summary>
/// Noise standard-deviation field that is only enabled (selectable/adjustable, drawn in its
/// normal color) while Noise is True. While Noise is False, it's grayed out and skipped by
/// encoder selection since it has no effect on the active sensor.
/// </summary>
///
class NoiseStdDevField : public FloatDisplayEditableField
{
public:
   using FloatDisplayEditableField::FloatDisplayEditableField;

   bool isEnabled() const override
   {
      return noiseEnabled != 0;
   }
};

Format noiseStdDevFormat("####.#");
float noiseStdDevValue = TestSensorConfig::NOISE_STDDEV;
float lastNoiseStdDevValue = TestSensorConfig::NOISE_STDDEV;
NoiseStdDevField noiseStdDevField("StdDev", &noiseStdDevValue,
   TestSensorConfig::NOISE_MIN_STDDEV, TestSensorConfig::NOISE_MAX_STDDEV,
   TestSensorConfig::NOISE_STDDEV_STEP, TestSensorConfig::NOISE_STDDEV, noiseStdDevFormat);

// ----------- Section Headers
// testFunctionField starts the "Test Function" section (shared by the always-present Source
// row, the source-specific configuration rows, and the Noise/StdDev rows); plotTypeField
// starts the "Plot" section; rateField starts the "Measured" section. Only a section's first
// field needs setSection() - later fields in the same section are drawn without a header.
// Applied here via immediately-invoked lambdas so the sections are set before any table
// layout/size queries happen in setup().
static const bool sectionsInitialized = []()
{
   testFunctionField.setSection("Test Function");
   plotTypeField.setSection("Plot");
   rateField.setSection("Measured");
   return true;
}();

DisplayEditableField* defaultStatusFields[] = { &testFunctionField, &noiseEnabledField, &noiseStdDevField, &plotTypeField, &plotXSizeField, &plotYSizeField, &displayModeField, &statsModeField, &rateField, &memoryField };
DisplayEditableField* constantStatusFields[] = { &testFunctionField, &constantValueField, &noiseEnabledField, &noiseStdDevField, &plotTypeField, &plotXSizeField, &plotYSizeField, &displayModeField, &statsModeField, &rateField, &memoryField };
DisplayEditableField* sinStatusFields[] = { &testFunctionField, &sinTimeSourceField, &sinPeriodField, &noiseEnabledField, &noiseStdDevField, &plotTypeField, &plotXSizeField, &plotYSizeField, &displayModeField, &statsModeField, &rateField, &memoryField };
DisplayEditableTable statusTable(&arduino, PREF_NAMESPACE, defaultStatusFields,
   sizeof(defaultStatusFields) / sizeof(defaultStatusFields[0]), 0, HEADER_HEIGHT);

// ----------- Test State
RollingRate updateRate(RATE_WINDOW_SAMPLES);
// Allocated in setup(), after startFreeHeapBytes is recorded, so the plot's own memory usage
// is included in the measured memory delta. Held through the shared IScatterPlot/
// IScatterPlotSeries interface so either concrete plot type (ScatterPlot or
// TimedScatterPlot) can be swapped in by applyPlotType() without the rest of the sketch
// needing to know which one is active.
IScatterPlot* scatterPlot = nullptr;
IScatterPlotSeries* sampleSeries = nullptr;

size_t sampleCount = 0;
bool sensorReady = false;
bool testComplete = false;
unsigned long testStartMs = 0;

// Throttles statusTable.draw() (called from updateRateReadout() during sampling) to every
// STATUS_DRAW_INTERVAL_MS, since redrawing the whole table on every render() is unrelated
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
/// Updates the live update-rate row in the status table, redrawing it at most every
/// STATUS_DRAW_INTERVAL_MS (see statusDrawTimer) so the redraw itself doesn't add overhead
/// on every sample-driven render() call.
/// </summary>
///
void updateRateReadout()
{
   rateValue = updateRate.get();
   memoryDeltaKb = (float)((int32_t)startFreeHeapBytes - (int32_t)ESP.getFreeHeap()) / 1024.0f;

   if (statusDrawTimer.ready())
   {
      statusTable.draw();
   }
}

///
/// <summary>
/// Stops the test, prints a final summary to serial, and updates the rate readout line on
/// the display to show the final result.
/// </summary>
///
void finishTest()
{
   testComplete = true;

   float finalRate = updateRate.get();
   float elapsedSeconds = (millis() - testStartMs) / 1000.0f;

   Serial.println();
   Serial.println("Scatter plot profiler complete");
   SerialX::print("Total Samples", 20);
   SerialX::println(sampleCount, 20);
   SerialX::print("Final Rate", 20);
   SerialX::println(String(finalRate, 1) + "/s", 20);
   SerialX::print("Elapsed", 20);
   SerialX::println(String(elapsedSeconds, 1) + "s", 20);

   arduino.setTextSize(2);
   rateValue = finalRate;
   memoryDeltaKb = (float)((int32_t)startFreeHeapBytes - (int32_t)ESP.getFreeHeap()) / 1024.0f;
   statusTable.draw();
}

///
/// <summary>
/// Computes the scatter plot's rectangle from the currently selected X/Y size percentages
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

	uint8_t xPercent = PLOT_SIZE_PERCENTS[constrain(plotXSizeIndex, 0L, (long)(NUM_PLOT_SIZES - 1))];
	uint8_t yPercent = PLOT_SIZE_PERCENTS[constrain(plotYSizeIndex, 0L, (long)(NUM_PLOT_SIZES - 1))];

	int16_t plotWidth = (int16_t)((int32_t)availableWidth * xPercent / 100);
	int16_t plotHeight = (int16_t)((int32_t)availableHeight * yPercent / 100);

	int16_t plotX = plotAreaX + (availableWidth - plotWidth) / 2;
	int16_t plotY = HEADER_HEIGHT + (availableHeight - plotHeight) / 2;

	static int16_t lastPlotAreaX = -1;
	static int16_t lastAvailableWidth = -1;
	static int16_t lastAvailableHeight = -1;
	bool availableAreaChanged = (plotAreaX != lastPlotAreaX) || (availableWidth != lastAvailableWidth) || (availableHeight != lastAvailableHeight);
	if (availableAreaChanged)
	{
		arduino.fillRect(plotAreaX, HEADER_HEIGHT, availableWidth, availableHeight, PLOT_BACKGROUND_COLOR);
		lastPlotAreaX = plotAreaX;
		lastAvailableWidth = availableWidth;
		lastAvailableHeight = availableHeight;
	}

	return Rect16{ (uint16_t)plotX, (uint16_t)plotY, (uint16_t)plotWidth, (uint16_t)plotHeight };
}

///
/// <summary>
/// (Re)creates the active plot at its current rectangle (see computePlotRect()), using
/// either a ScatterPlot ("Fixed" type) or TimedScatterPlot ("Timed" type) as selected by
/// plotTypeField, and (re)creates its single sample series. Both concrete plot types are
/// held through the shared IScatterPlot/IScatterPlotSeries interface (see IScatterPlot.h),
/// so the rest of the sketch (sampling loop, clearPlot(), etc.) doesn't need to know which
/// one is active. Called whenever the plot type or size changes, and once from setup().
/// </summary>
///
void recreatePlot()
{
	delete scatterPlot;

	Rect16 rect = computePlotRect();

	if (plotTypeIndex == 0)
	{
		ScatterPlot* fixedPlot = new ScatterPlot(&arduino, rect);
		fixedPlot->setXAxisFormat(xAxisFormat);
		ScatterPlotSeries* fixedSeries = fixedPlot->createSeries(MAX_SAMPLES);
		fixedSeries->movingSampleSize = (float)MAX_SAMPLES / 10.0f;
		sampleSeries = fixedSeries;
		scatterPlot = fixedPlot;
	}
	else
	{
		TimedScatterPlot* timedPlot = new TimedScatterPlot(&arduino, rect, TIMED_HISTORY_S * 1000UL);
		sampleSeries = timedPlot->createSeries();
		scatterPlot = timedPlot;
	}

	scatterPlot->setColors(PLOT_BACKGROUND_COLOR, PLOT_BACKGROUND_COLOR, Color::GRAY, Color::LABEL);

	applyDisplayMode();
	applyStatsMode();

	if (sensor != nullptr)
	{
		scatterPlot->setYAxisFormat(*sensor->getFormat());
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
	long index = constrain(displayModeIndex, 0L, (long)(NUM_DISPLAY_MODES - 1));

	sampleSeries->showPoints = (index == 0);
	sampleSeries->showLines = (index == 1);
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
	long index = constrain(statsModeIndex, 0L, (long)(NUM_STATS_MODES - 1));

	sampleSeries->showMovingAverage = (index == 1 || index == 3);
	sampleSeries->showStdDevBand = (index == 2 || index == 3);
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

	sensor->setNoiseStdDev(noiseEnabled != 0 ? noiseStdDevValue : 0.0f);
}

///
/// <summary>
/// Selects the sensor for the current testFunctionIndex, begins it, applies noise, and swaps
/// the status table to the field set appropriate for that sensor (which may change the
/// table's width). Split out from applyTestFunction() so recreatePlot() can be called
/// afterward with the correct table width already in place.
/// </summary>
///
void selectTestFunction()
{
	sensor = TEST_FUNCTION_SENSORS[testFunctionIndex];
	sensorReady = sensor->begin();
	applyNoise();

	if (sensor == &constantSensor)
	{
		statusTable.setFields(constantStatusFields, sizeof(constantStatusFields) / sizeof(constantStatusFields[0]));
	}
	else if (sensor == &sinSensor)
	{
		statusTable.setFields(sinStatusFields, sizeof(sinStatusFields) / sizeof(sinStatusFields[0]));
	}
	else
	{
		statusTable.setFields(defaultStatusFields, sizeof(defaultStatusFields) / sizeof(defaultStatusFields[0]));
	}
	statusTable.load();
}

///
/// <summary>
/// Switches the active sensor to the currently selected test function and recreates the plot
/// (which also resets sample count, series data, update rate, and completion status) so the
/// plot starts a fresh run. Called both at startup and whenever the live test function field
/// changes. Also swaps in the source-specific configuration rows (e.g. Value for Constant,
/// Sampling and Period for Sin) alongside the always-present Source and Rate rows.
/// </summary>
///
void applyTestFunction()
{
	selectTestFunction();
	recreatePlot();

	sampleCount = 0;
	testComplete = false;
	updateRate.reset();

	rateValue = updateRate.get();
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

	testStartMs = millis();
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
	testComplete = false;
	updateRate.reset();

	scatterPlot->clear();

	rateValue = updateRate.get();
	arduino.setTextSize(2);
	statusTable.draw();

	testStartMs = millis();
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

	startFreeHeapBytes = ESP.getFreeHeap();

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

		statusTable.save();
		arduino.setTextSize(2);

		if (testFunctionIndex != lastTestFunctionIndex)
		{
			lastTestFunctionIndex = testFunctionIndex;
			applyTestFunction();
		}
		else if (plotTypeIndex != lastPlotTypeIndex)
		{
			lastPlotTypeIndex = plotTypeIndex;
			recreatePlot();
			sampleCount = 0;
			testComplete = false;
			updateRate.reset();
			testStartMs = millis();
			statusTable.draw();
		}
		else if (plotXSizeIndex != lastPlotXSizeIndex || plotYSizeIndex != lastPlotYSizeIndex)
		{
			lastPlotXSizeIndex = plotXSizeIndex;
			lastPlotYSizeIndex = plotYSizeIndex;
			recreatePlot();
			sampleSeries->clear();
			sampleCount = 0;
			testComplete = false;
			updateRate.reset();
			testStartMs = millis();
			statusTable.draw();
		}
		else if (noiseEnabled != lastNoiseEnabled || noiseStdDevValue != lastNoiseStdDevValue)
		{
			lastNoiseEnabled = noiseEnabled;
			lastNoiseStdDevValue = noiseStdDevValue;
			applyNoise();
			statusTable.draw();
		}
		else if (displayModeIndex != lastDisplayModeIndex)
		{
			lastDisplayModeIndex = displayModeIndex;
			applyDisplayMode();
			scatterPlot->invalidate();
			scatterPlot->render();
			statusTable.draw();
		}
		else if (statsModeIndex != lastStatsModeIndex)
		{
			lastStatsModeIndex = statsModeIndex;
			applyStatsMode();
			scatterPlot->invalidate();
			scatterPlot->render();
			statusTable.draw();
		}
		else
		{
			statusTable.draw();
		}
	}

	if (!sensorReady || testComplete)
	{
		return;
	}

	float value = sensor->get();
	if (!isfinite(value))
	{
		return;
	}

	bool isTimed = (plotTypeIndex != 0);

	if (isTimed || sampleCount < MAX_SAMPLES)
	{
		sampleSeries->add(value);
		sampleCount++;
	}

	if ((sampleCount % 10) == 0)
	{
		updateRate.tick();
		scatterPlot->render();
		updateRateReadout();
	}

	if (isTimed)
	{
		return;
	}

	bool rateBelowStop = (sampleCount >= MIN_SAMPLES_BEFORE_STOP_CHECK) && (updateRate.get() <= STOP_RATE_PER_SEC);
	if (rateBelowStop)
	{
		finishTest();
	}
	else if (sampleCount >= MAX_SAMPLES)
	{
		finishTest();
	}
}
