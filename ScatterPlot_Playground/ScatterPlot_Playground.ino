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
#include "DisplayTableCellEditor.h"
#include "DisplayTableEditor.h"
#include "RollingRate.h"
#include "ScatterPlot.h"
#include "SerialX.h"
#include "TestSensor.h"
#include "Timer.h"
#include "Util.h"

// ----------- Test Function Selection
// The available mock test functions the user can select from at startup via a DisplayTableEditor.
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
// once full. See MaxSamplesCell/maxSamplesIndex below.
constexpr size_t MAX_SAMPLES_OPTIONS[] = { 500, 1000, 2000, 3000, 5000, 10000 };
constexpr size_t NUM_MAX_SAMPLES_OPTIONS = sizeof(MAX_SAMPLES_OPTIONS) / sizeof(MAX_SAMPLES_OPTIONS[0]);
constexpr size_t DEFAULT_MAX_SAMPLES_INDEX = 1; // 1000

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
long testFunctionIndex = 0;
long lastTestFunctionIndex = 0;
EnumCellEditor testFunctionCell(&testFunctionIndex,TEST_FUNCTION_LABELS, 0, "######");
float rateValue = 0.0f;
ReadOnlyCell rateCell(&rateValue, Format("####/s", Format::Alignment::LEFT));

uint32_t startFreeHeapBytes = 0;
float memoryDeltaKb = 0.0f;
ReadOnlyCell memoryCell(&memoryDeltaKb, "###.# kb");

///
/// <summary>
/// Plot-size-selection field shared by the X Size and Y Size rows. Steps through PLOT_SIZE_PERCENTS
/// by index (like EnumCellEditor), but formats its label directly from the selected
/// percentage instead of a separate parallel string-label array.
/// </summary>
///
class PlotSizeCell : public IntCellEditor
{
public:
   PlotSizeCell(long* value, const char* formatStr)
      : IntCellEditor(value, 0, (long)NUM_PLOT_SIZES - 1, 1, 0, formatStr)
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

long plotXSizeIndex = 0;
long plotYSizeIndex = 0;
long lastPlotXSizeIndex = 0;
long lastPlotYSizeIndex = 0;
PlotSizeCell plotXSizeCell(&plotXSizeIndex, "###%    ");
PlotSizeCell plotYSizeCell(&plotYSizeIndex, "###%    ");

///
/// <summary>
/// Rolling-sample-count-selection field. Steps through MAX_SAMPLES_OPTIONS by index (like
/// PlotSizeCell), formatting its label directly from the selected sample count.
/// </summary>
///
class MaxSamplesCell : public IntCellEditor
{
public:
   MaxSamplesCell(long* value, const char* formatStr)
      : IntCellEditor(value, 0, (long)NUM_MAX_SAMPLES_OPTIONS - 1, 1, 0, formatStr)
   {
   }

   void adjust(int32_t direction) override
   {
      long newValue = (*_value + (direction > 0 ? 1 : -1) + (long)NUM_MAX_SAMPLES_OPTIONS) % (long)NUM_MAX_SAMPLES_OPTIONS;
      *_value = newValue;
   }

   std::string valueText() override
   {
      long index = constrain(*_value, 0L, (long)(NUM_MAX_SAMPLES_OPTIONS - 1));
      return _format.toString((double)MAX_SAMPLES_OPTIONS[index]);
   }
};

long maxSamplesIndex = DEFAULT_MAX_SAMPLES_INDEX;
long lastMaxSamplesIndex = DEFAULT_MAX_SAMPLES_INDEX;
MaxSamplesCell maxSamplesCell(&maxSamplesIndex, "#####   ");

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
long displayModeIndex = 0;
long lastDisplayModeIndex = 0;
EnumCellEditor displayModeCell(&displayModeIndex,
   DISPLAY_MODE_LABELS, 0, "########");

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
long statsModeIndex = 0;
long lastStatsModeIndex = 0;
EnumCellEditor statsModeCell(&statsModeIndex,
   STATS_MODE_LABELS, 0, "########");

// ----------- Source-Specific Configuration Fields
// The Constant source lets the user set its value directly; the Sin source lets the user
// adjust its period (always fixed-step; see SinPeriodCell). Each source's field array is
// swapped into statusTable by applyTestFunction() below.
FloatCellEditor constantValueCell(&constantSensor.value,
   TestSensorConfig::CONSTANT_MIN_VALUE, TestSensorConfig::CONSTANT_MAX_VALUE,
   TestSensorConfig::CONSTANT_STEP, TestSensorConfig::CONSTANT_VALUE, "####.#");

///
/// <summary>
/// Sin period field that displays whole-number values as a plain sample count with no unit,
/// since the period doesn't correspond to real elapsed time - it advances by a fixed step per
/// sample (see SinTestSensor::TIME_SOURCE_FIXED_STEP, the sensor's only sampling mode).
/// </summary>
///
class SinPeriodCell : public FloatCellEditor
{
public:
   using FloatCellEditor::FloatCellEditor;

   void adjust(int32_t direction) override
   {
      long samples = lroundf(*_value / TestSensorConfig::SIN_FIXED_STEP_S);
      samples += direction * TestSensorConfig::SIN_FIXED_PERIOD_STEP_SAMPLES;
      samples = constrain(samples, TestSensorConfig::SIN_FIXED_MIN_PERIOD_SAMPLES, TestSensorConfig::SIN_FIXED_MAX_PERIOD_SAMPLES);
      *_value = samples * TestSensorConfig::SIN_FIXED_STEP_S;
   }

   std::string valueText() override
   {
      long samples = lroundf(*_value / TestSensorConfig::SIN_FIXED_STEP_S);
      return _format.toString(String(samples));
   }
};

SinPeriodCell sinPeriodCell(&sinSensor.periodS,
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

long noiseEnabled = 0;
long lastNoiseEnabled = 0;
EnumCellEditor noiseEnabledCell(&noiseEnabled,
   NOISE_ENABLED_LABELS, 0, "########");

///
/// <summary>
/// Noise standard-deviation field that is only enabled (selectable/adjustable, drawn in its
/// normal color) while Noise is True. While Noise is False, it's grayed out and skipped by
/// encoder selection since it has no effect on the active sensor.
/// </summary>
///
class NoiseStdDevCell : public FloatCellEditor
{
public:
   using FloatCellEditor::FloatCellEditor;

   bool isEnabled() const override
   {
      return noiseEnabled != 0;
   }
};

float noiseStdDevValue = TestSensorConfig::NOISE_STDDEV;
float lastNoiseStdDevValue = TestSensorConfig::NOISE_STDDEV;
NoiseStdDevCell noiseStdDevCell(&noiseStdDevValue,
   TestSensorConfig::NOISE_MIN_STDDEV, TestSensorConfig::NOISE_MAX_STDDEV,
   TestSensorConfig::NOISE_STDDEV_STEP, TestSensorConfig::NOISE_STDDEV, "####.#");

// ----------- Section Headers
// The Test Function section is shared by the always-present Source row, the source-specific
// configuration rows, and the Noise/StdDev rows; the Plot section starts at the plot-size rows;
// the Measured section starts at the FPS row. Section headers are their own label-only
// TableEditorRow entries (no cell), rendered above the rows that follow them.

TableEditorRow defaultStatusCells[] =
{
   { "Test Function" },
   { "Source", &testFunctionCell },
   { "Noise", &noiseEnabledCell },
   { "StdDev", &noiseStdDevCell },
   { "Plot" },
   { "X Size", &plotXSizeCell },
   { "Y Size", &plotYSizeCell },
   { "Samples", &maxSamplesCell },
   { "Display", &displayModeCell },
   { "Stats", &statsModeCell },
   { "Measured" },
   { "FPS", &rateCell },
   { "Memory", &memoryCell },
};
TableEditorRow constantStatusCells[] =
{
   { "Test Function" },
   { "Source", &testFunctionCell },
   { "Value", &constantValueCell },
   { "Noise", &noiseEnabledCell },
   { "StdDev", &noiseStdDevCell },
   { "Plot" },
   { "X Size", &plotXSizeCell },
   { "Y Size", &plotYSizeCell },
   { "Samples", &maxSamplesCell },
   { "Display", &displayModeCell },
   { "Stats", &statsModeCell },
   { "Measured" },
   { "FPS", &rateCell },
   { "Memory", &memoryCell },
};
TableEditorRow sinStatusCells[] =
{
   { "Test Function" },
   { "Source", &testFunctionCell },
   { "Period", &sinPeriodCell },
   { "Noise", &noiseEnabledCell },
   { "StdDev", &noiseStdDevCell },
   { "Plot" },
   { "X Size", &plotXSizeCell },
   { "Y Size", &plotYSizeCell },
   { "Samples", &maxSamplesCell },
   { "Display", &displayModeCell },
   { "Stats", &statsModeCell },
   { "Measured" },
   { "FPS", &rateCell },
   { "Memory", &memoryCell },
};
DisplayTableEditor statusTable(&arduino, PREF_NAMESPACE, defaultStatusCells,
   0, HEADER_HEIGHT, 2, DisplayTable::Alignment::RIGHT);

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
/// Recomputes memoryDeltaKb from the current free heap relative to startFreeHeapBytes.
/// Called immediately after any plot recreation (so the Memory row reflects the new
/// plot's allocation right away) as well as periodically from updateRateReadout().
/// </summary>
///
void updateMemoryReadout()
{
   memoryDeltaKb = (float)((int32_t)startFreeHeapBytes - (int32_t)getTotalFreeHeap()) / 1024.0f;
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
   rateValue = updateRate.get();
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

	uint8_t xPercent = PLOT_SIZE_PERCENTS[constrain(plotXSizeIndex, 0L, (long)(NUM_PLOT_SIZES - 1))];
	uint8_t yPercent = PLOT_SIZE_PERCENTS[constrain(plotYSizeIndex, 0L, (long)(NUM_PLOT_SIZES - 1))];

	int16_t plotWidth = (int16_t)((int32_t)availableWidth * xPercent / 100);
	int16_t plotHeight = (int16_t)((int32_t)availableHeight * yPercent / 100);

	int16_t plotX = plotAreaX + (availableWidth - plotWidth) / 2;
	int16_t plotY = HEADER_HEIGHT + (availableHeight - plotHeight) / 2;

	static int16_t lastPlotAreaX = -1;
	static int16_t lastAvailableWidth = -1;
	static int16_t lastAvailableHeight = -1;
	static uint8_t lastXPercent = 0;
	static uint8_t lastYPercent = 0;
	bool availableAreaChanged = (plotAreaX != lastPlotAreaX) || (availableWidth != lastAvailableWidth) || (availableHeight != lastAvailableHeight)
		|| (xPercent != lastXPercent) || (yPercent != lastYPercent);
	if (availableAreaChanged)
	{
		arduino.fillRect(plotAreaX, HEADER_HEIGHT, availableWidth, availableHeight, PLOT_BACKGROUND_COLOR);
		lastPlotAreaX = plotAreaX;
		lastAvailableWidth = availableWidth;
		lastAvailableHeight = availableHeight;
		lastXPercent = xPercent;
		lastYPercent = yPercent;
	}

	return Rect16{ (uint16_t)plotX, (uint16_t)plotY, (uint16_t)plotWidth, (uint16_t)plotHeight };
}

///
/// <summary>
/// (Re)creates the active plot at its current rectangle (see computePlotRect()), using a
/// single rolling-count ScatterPlot series of the currently selected sample count (see
/// MAX_SAMPLES_OPTIONS/ScatterPlot::createRollingSeries()): points fill in from the left and,
/// once full, the oldest point scrolls off as each new one is added. Called whenever the plot
/// size or sample count changes, and once from setup().
/// </summary>
///
void recreatePlot()
{
	delete scatterPlot;

	Rect16 rect = computePlotRect();

	scatterPlot = new ScatterPlot(&arduino, rect, "#####", "##.#");

	size_t maxSamples = MAX_SAMPLES_OPTIONS[constrain(maxSamplesIndex, 0L, (long)(NUM_MAX_SAMPLES_OPTIONS - 1))];
	ScatterPlotSeries* rollingSeries = scatterPlot->createRollingSeries(maxSamples);
	rollingSeries->movingSampleSize = (float)maxSamples / 10.0f;
	sampleSeries = rollingSeries;

	scatterPlot->setColors(PLOT_BACKGROUND_COLOR, PLOT_BACKGROUND_COLOR, Color::GRAY, Color::LABEL);
	scatterPlot->setYAxisMode(ScatterPlot::AxisMode::GROW_ONLY);

	applyDisplayMode();
	applyStatsMode();

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
		statusTable.setFields(constantStatusCells);
	}
	else if (sensor == &sinSensor)
	{
		statusTable.setFields(sinStatusCells);
	}
	else
	{
		statusTable.setFields(defaultStatusCells);
	}
	statusTable.load();
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

	rateValue = updateRate.get();
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

	rateValue = updateRate.get();
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

	// statusTable.load() (called from applyTestFunction() -> selectTestFunction()) may have
	// restored persisted values that differ from these compile-time defaults. Re-sync the
	// change-tracking variables to the now-current values so the first encoder turn (which
	// only moves the selection highlight via selectNext(), not a value via adjust()) doesn't
	// spuriously look like a value change and trigger an unwanted recreatePlot()/applyTestFunction().
	lastTestFunctionIndex = testFunctionIndex;
	lastPlotXSizeIndex = plotXSizeIndex;
	lastPlotYSizeIndex = plotYSizeIndex;
	lastMaxSamplesIndex = maxSamplesIndex;
	lastNoiseEnabled = noiseEnabled;
	lastNoiseStdDevValue = noiseStdDevValue;
	lastDisplayModeIndex = displayModeIndex;
	lastStatsModeIndex = statsModeIndex;

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
		else if (plotXSizeIndex != lastPlotXSizeIndex || plotYSizeIndex != lastPlotYSizeIndex || maxSamplesIndex != lastMaxSamplesIndex)
		{
			lastPlotXSizeIndex = plotXSizeIndex;
			lastPlotYSizeIndex = plotYSizeIndex;
			lastMaxSamplesIndex = maxSamplesIndex;
			recreatePlot();
			sampleSeries->clear();
			sampleCount = 0;
			updateRate.reset();
			rateValue = updateRate.get();
			updateMemoryReadout();
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
			scatterPlot->draw();
			statusTable.draw();
		}
		else if (statsModeIndex != lastStatsModeIndex)
		{
			lastStatsModeIndex = statsModeIndex;
			applyStatsMode();
			scatterPlot->invalidate();
			scatterPlot->draw();
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
