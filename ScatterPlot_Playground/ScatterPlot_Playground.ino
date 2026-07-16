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
// Serial output logs sample count, update rate, and elapsed time at SERIAL_PRINT_RATE_PER_SEC
// while the test runs, and prints a final summary (including which stop condition was hit) once
// the test completes. The display shows a title, a status table on the left (sensor type, live
// rate, and sample count), and the scatter plot to the right of the table.
//

// System/standard library headers
#include <math.h>
#include <Wire.h>

// Local library headers (from libraries/Woyak)
#include "ESP32_S3_Playground.h"
#include "DisplayTable.h"
#include "RollingRate.h"
#include "ScatterPlot.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "TestSensor.h"
#include "Timer.h"
#include "Util.h"

// ----------- Data Source
// One-line data source switch for this sketch only. Change only DATA_SOURCE_TYPE below;
// sensor is declared as that type directly. Use sensor.sensorType() to display the
// active source's type name at runtime.
// #define DATA_SOURCE_TYPE ConstantTestSensor
#define DATA_SOURCE_TYPE RandomTestSensor
// #define DATA_SOURCE_TYPE NormalTestSensor
// #define DATA_SOURCE_TYPE SinTestSensor
// #define DATA_SOURCE_TYPE SinWithNormalNoiseTestSensor

// ----------- Test Parameters
constexpr float STOP_RATE_PER_SEC = 10.0f;
constexpr uint16_t RATE_WINDOW_SAMPLES = 10;
constexpr size_t MIN_SAMPLES_BEFORE_STOP_CHECK = RATE_WINDOW_SAMPLES * 10;
constexpr size_t MAX_SAMPLES = 30000; // safety cap in case the rate never reaches STOP_RATE_PER_SEC

// ----------- Display Geometry
// Matches the ESP32_S3_Playground board's LGX_Hosyond_ST7796 display in landscape orientation.
constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint16_t DISPLAY_HEIGHT = 320;
constexpr uint16_t HEADER_HEIGHT = 3 * 8 + 4; // title (size 3) plus padding
constexpr uint16_t TABLE_PLOT_GAP = 10; // gap between the status table and the scatter plot

// ----------- Serial Output
constexpr uint8_t SERIAL_PRINT_RATE_PER_SEC = 1;
constexpr SerialTable::Column RESULT_COLUMNS[] = {
   { "Samples", 12 },
   { "Rate(/s)", 12 },
   { "Elapsed(s)", 12 },
};
constexpr size_t NUM_RESULT_COLUMNS = sizeof(RESULT_COLUMNS) / sizeof(RESULT_COLUMNS[0]);

// ----------- The Board
ESP32_S3_Playground arduino;
DATA_SOURCE_TYPE sensor;

// ----------- Display Formats
Format rateFormat("####/s", Format::Alignment::LEFT);
Format xAxisFormat("#####");

// ----------- Test State
RollingRate updateRate(RATE_WINDOW_SAMPLES);
DisplayTable statusTable(&arduino, 0, HEADER_HEIGHT, 2);
ScatterPlot scatterPlot(&arduino, 0, HEADER_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT);
ScatterPlotSeries* sampleSeries = nullptr;
RateTimer serialPrintTimer(SERIAL_PRINT_RATE_PER_SEC);
SerialTable resultTable("Scatter Plot Update Rate", RESULT_COLUMNS, NUM_RESULT_COLUMNS);

size_t sampleCount = 0;
bool sensorReady = false;
bool testComplete = false;
unsigned long testStartMs = 0;

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
/// Updates the live update-rate and sample-count rows in the status table. DisplayTable
/// only redraws a row's value when it actually changes, so this does not flicker.
/// </summary>
///
void updateRateReadout()
{
   statusTable.updateValue(1, updateRate.get());
   statusTable.draw();
}

///
/// <summary>
/// Prints one row of sample count, update rate, and elapsed time to serial.
/// </summary>
///
void printSerialRow()
{
	float elapsedSeconds = (millis() - testStartMs) / 1000.0f;
	resultTable.printRow(
		sampleCount,
		SerialTable::fixed(updateRate.get(), 1),
		SerialTable::fixed(elapsedSeconds, 1));
}

///
/// <summary>
/// Stops the test, prints a final summary to serial (including which stop condition was
/// hit), and updates the rate readout line on the display to show the final result.
/// </summary>
/// <param name="reachedStopRate">True when the test stopped because the update rate fell
/// to STOP_RATE_PER_SEC; false when it stopped because the sample buffer safety cap was
/// reached instead.</param>
///
void finishTest(bool reachedStopRate)
{
   testComplete = true;
   printSerialRow();

   float finalRate = updateRate.get();
   float elapsedSeconds = (millis() - testStartMs) / 1000.0f;

   Serial.println();
   Serial.println("Scatter plot profiler complete");
   Serial.println(reachedStopRate ? "Stop reason: update rate reached target" : "Stop reason: sample cap reached");
   SerialX::print("Total Samples", 20);
   SerialX::println(sampleCount, 20);
   SerialX::print("Final Rate", 20);
   SerialX::println(String(finalRate, 1) + "/s", 20);
   SerialX::print("Elapsed", 20);
   SerialX::println(String(elapsedSeconds, 1) + "s", 20);

   arduino.setTextSize(2);
   statusTable.updateValue(1, finalRate);
   statusTable.draw();
}

void setup()
{
	SerialX::begin();
	Wire.begin();

	arduino.begin();
	drawTitle();

	sampleSeries = scatterPlot.createSeries(MAX_SAMPLES);
	sampleSeries->showPoints = true;
	scatterPlot.setXAxisFormat(xAxisFormat);
	scatterPlot.setYAxisFormat(*sensor.getFormat());

	statusTable.addRow("Sensor", Format(8), Color::LABEL, Color::VALUE);
	statusTable.addRow("Rate", rateFormat, Color::LABEL, Color::VALUE);
	statusTable.updateValue(0, String(sensor.sensorType()));
	statusTable.draw();

	int16_t plotX = statusTable.getWidth() + TABLE_PLOT_GAP;
	scatterPlot.setRect(plotX, HEADER_HEIGHT, DISPLAY_WIDTH - plotX, DISPLAY_HEIGHT - HEADER_HEIGHT);

	sensorReady = sensor.begin();
	if (!sensorReady)
	{
		arduino.setTextSize(2);
		arduino.setCursor(0, HEADER_HEIGHT);
		arduino.println("Sensor init failed", Color::RED);
		Serial.println("Error: sensor initialization failed");
		return;
	}

	resultTable.printHeader();
	testStartMs = millis();
}

void loop()
{
	if (arduino.buttonA.wasPressed())
	{
		Util::reset();
	}

	if (!sensorReady || testComplete)
	{
		return;
	}

	float value = sensor.get();
	if (!isfinite(value))
	{
		return;
	}

	if (sampleCount < MAX_SAMPLES)
	{
		sampleSeries->add(static_cast<float>(sampleCount + 1), value);
		sampleCount++;
	}

	if ((sampleCount % 10) == 0)
	{
		updateRate.tick();
		scatterPlot.render();
		updateRateReadout();
	}

	if (serialPrintTimer.ready())
	{
		printSerialRow();
	}

	bool rateBelowStop = (sampleCount >= MIN_SAMPLES_BEFORE_STOP_CHECK) && (updateRate.get() <= STOP_RATE_PER_SEC);
	if (rateBelowStop)
	{
		finishTest(true);
	}
	else if (sampleCount >= MAX_SAMPLES)
	{
		finishTest(false);
	}
}
