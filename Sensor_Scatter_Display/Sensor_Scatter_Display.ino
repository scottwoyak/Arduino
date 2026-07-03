//
// Continuously samples temperature and renders a rolling scatter plot on the Feather display.
// Press button A to toggle between the scatter plot and a histogram view.
//
// Samples are captured at a fixed interval and stored in a timed buffer.
// In scatter mode the display shows the past window of data with min/max on the y axis and
// sample rate in the top-right corner.
// In histogram mode the display shows a bar histogram of the rolling sample distribution with
// min/max bin labels along the x axis and sample count in the top-right corner.
// Invalid sensor reads are ignored, and serial output periodically reports summary values.
//

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "Feather.h"
#include "SerialX.h"
#include "TempSensor.h"
#include "Timer.h"
#include "RollingRate.h"
#include "TimedScatterPlot.h"
#include "TimedHistogram.h"
#include "TimedValues.h"

constexpr uint16_t SAMPLE_INTERVAL_MS = 0;
constexpr uint16_t DISPLAY_INTERVAL_MS = 100;
constexpr uint16_t SERIAL_INTERVAL_MS = 5000;
constexpr unsigned long SAMPLE_PERIOD_S = 60;
constexpr unsigned long HISTOGRAM_PERIOD_S = 6;
constexpr uint16_t BIN_COUNT = 40;
constexpr float SHT45_TEMP_RESOLUTION_F = 0.0049f;

Feather feather;
TempSensor sensor;
Timer sampleTimer(SAMPLE_INTERVAL_MS);
Timer displayTimer(DISPLAY_INTERVAL_MS);
Timer serialTimer(SERIAL_INTERVAL_MS);
RollingRate sampleRate;

TimedValues samples(SAMPLE_PERIOD_S * 1000UL, 256);

enum class DisplayMode : uint8_t { Scatter, Histogram };

Format sampleRateFormat("###/s", Format::Alignment::RIGHT);

TimedScatterPlot scatterPlot(feather, samples, SAMPLE_PERIOD_S * 1000UL, 0.0f);
TimedHistogram histogram(feather, samples, BIN_COUNT, HISTOGRAM_PERIOD_S * 1000UL, SHT45_TEMP_RESOLUTION_F);

DisplayMode displayMode = DisplayMode::Scatter;

void addSample(float value)
{
   sampleRate.tick();
   samples.set(value);
}

void drawHeader()
{
   feather.setTextSize(2);
   feather.setCursor(0, 0);
   feather.println(displayMode == DisplayMode::Scatter ? "Sensor Scatter" : "Histogram", Color::HEADING);
}

void setup()
{
   SerialX::begin();
   Wire.begin();
   feather.begin();

   feather.clearDisplay();
   drawHeader();

   if (!sensor.begin())
   {
	  feather.setTextSize(2);
	  feather.setCursor(0, 0);
	  feather.println("Sensor init failed", Color::RED);
	  Serial.println("Sensor initialization failed.");
	  return;
   }
}

void loop()
{
   if (feather.buttonA.wasPressed())
   {
	  displayMode = (displayMode == DisplayMode::Scatter) ? DisplayMode::Histogram : DisplayMode::Scatter;
	  feather.clearDisplay();
	  drawHeader();
	  if (displayMode == DisplayMode::Histogram)
	  {
		 histogram.reset();
	  }
   }

   if (sampleTimer.ready())
   {
	  const float value = sensor.readTemperatureF();
	  if (isfinite(value))
	  {
		 addSample(value);
	  }
   }

   const bool shouldRender = (displayMode == DisplayMode::Scatter) || displayTimer.ready();
   if (shouldRender)
   {
	  feather.setTextSize(2);
	  feather.setCursor(0, 0);
	  feather.printR("", sampleRate.get(), sampleRateFormat, Color::GRAY);

	  if (displayMode == DisplayMode::Scatter)
	  {
		 scatterPlot.render();
	  }
	  else
	  {
		 histogram.render();
	  }
   }

   if (serialTimer.ready() && (samples.count() > 0))
   {
	  const float currentValue = samples.get(0);
	  size_t serialCount = samples.count();
	  Serial.print("Samples: ");
	  Serial.print(serialCount);
	  Serial.print(" Current: ");
	  Serial.print(currentValue, 3);
	  Serial.print(" Mode: ");
	  Serial.println(displayMode == DisplayMode::Scatter ? "Scatter" : "Histogram");
   }
}
