//
// Profiles how fast a ScatterPlot can be redrawn as its backing sample series grows.
//
// Continuously samples a TestSensor and appends each reading to a ScatterPlotSeries, calling
// ScatterPlot::render() after every 10th new sample. Because render() recomputes the shared
// axis range by scanning every point stored in the series on each call, the time per update
// grows as the series grows, so the achieved update rate falls over the course of the test.
// The test stops once the rolling update rate drops to STOP_RATE_PER_SEC, or once the sample
// buffer safety cap is reached, whichever comes first.
//
// Serial output logs sample count, update rate, and elapsed time at SERIAL_PRINT_RATE_PER_SEC
// while the test runs, and prints a final summary (including which stop condition was hit) once
// the test completes. The display shows a title, a live "Rate / Samples" readout that updates
// every iteration, and the scatter plot itself below it.
//

// System/standard library headers
#include <math.h>
#include <Wire.h>

// Local library headers (from libraries/Woyak)
#include "ESP32_S3_Playground.h"
#include "RollingRate.h"
#include "ScatterPlot.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "TestSensor.h"
#include "Timer.h"
#include "Util.h"

// ----------- Test Parameters
constexpr float STOP_RATE_PER_SEC = 10.0f;
constexpr uint16_t RATE_WINDOW_SAMPLES = 10;
constexpr size_t MIN_SAMPLES_BEFORE_STOP_CHECK = RATE_WINDOW_SAMPLES * 10;
constexpr size_t MAX_SAMPLES = 30000; // safety cap in case the rate never reaches STOP_RATE_PER_SEC

// ----------- Display Geometry
// Matches the ESP32_S3_Playground board's LGX_Hosyond_ST7796 display in landscape orientation.
constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint16_t DISPLAY_HEIGHT = 320;
constexpr uint16_t HEADER_HEIGHT = 3 * 8 + 2 * 8 + 4; // title (size 3) + rate line (size 2) plus padding

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
RandomTestSensor sensor;

// ----------- Display Formats
Format rateFormat("####/s", Format::Alignment::RIGHT);
Format countFormat("#####", Format::Alignment::RIGHT);

// ----------- Test State
RollingRate updateRate(RATE_WINDOW_SAMPLES);
ScatterPlot scatterPlot(&arduino, 0, HEADER_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT);
ScatterPlotSeries* sampleSeries = nullptr;
RateTimer serialPrintTimer(SERIAL_PRINT_RATE_PER_SEC);
SerialTable resultTable("Scatter Plot Update Rate", RESULT_COLUMNS, NUM_RESULT_COLUMNS);

size_t sampleCount = 0;
bool sensorReady = false;
bool testComplete = false;
unsigned long testStartMs = 0;
int16_t rateRowY = 0;

///
/// <summary>
/// Clears the display and draws the sketch title, recording the Y position of the
/// rate readout line drawn below it.
/// </summary>
///
void drawTitle()
{
   arduino.setTextSize(3);
   arduino.clearDisplay();
   arduino.println("Scatter Plot Profiler", Color::HEADING);

   rateRowY = arduino.getCursorY();
}

///
/// <summary>
/// Redraws the live update rate and sample count line. The fixed-width formats
/// overwrite their own background in place, so the row does not need to be cleared
/// first and does not flicker.
/// </summary>
///
void updateRateReadout()
{
   arduino.setTextSize(2);
   arduino.setCursor(0, rateRowY);
   arduino.print("Rate: ", updateRate.get(), rateFormat, Color::VALUE);
   arduino.println("  Samples: ", sampleCount, countFormat, Color::VALUE2);
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
   arduino.fillRect(0, rateRowY, DISPLAY_WIDTH, arduino.charH(), Color::BLACK);
   arduino.setCursor(0, rateRowY);
   arduino.print("Final: ", finalRate, rateFormat, Color::VALUE);
   arduino.println("  Samples: ", sampleCount, countFormat, Color::VALUE2);
}

void setup()
{
	SerialX::begin();
	Wire.begin();

	arduino.begin();
	drawTitle();

	sampleSeries = scatterPlot.createSeries(MAX_SAMPLES);
	sampleSeries->showPoints = true;

	sensorReady = sensor.begin();
	if (!sensorReady)
	{
		arduino.setTextSize(2);
		arduino.setCursor(0, rateRowY);
		arduino.println("Sensor init failed", Color::RED);
		Serial.println("Error: sensor initialization failed");
		return;
	}

	resultTable.printHeader();
	testStartMs = millis();
}

void loop()
{
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
		sampleSeries->add(static_cast<float>(sampleCount), value);
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
