/// <summary>
/// Subscribes to lake wave telemetry and renders a smoothed wave-height bar chart.
/// </summary>
/// <remarks>
/// Connects over WiFi/WebSocket, receives wave sensor values, applies short-term smoothing,
/// and draws real-time height updates on the display. Periodic serial diagnostics are logged
/// using a Timer-based interval.
/// </remarks>

// Undefine to use the remote server.
//#define TELEMETRY_LOCAL

#include <WiFi.h>

#include "BarChart.h"
#include "BufferedTimeSeries.h"
#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "RollingRate.h"
#include "RollingStats.h"
#include "SerialX.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "Util.h"
#include "WiFiSettings.h"

Arduino arduino;
RollingRate refreshRate(100);
TelemetrySubscriber client("Waves/Lake");
RollingStats sensorReadings(500);

constexpr unsigned long LOG_INTERVAL_MS = 1000;
constexpr unsigned long BUFFER_TIME_SPAN_MS = 2000;
constexpr unsigned long BUFFER_RESOLUTION_MS = 100;

BufferedTimeSeries waveHeight(BUFFER_TIME_SPAN_MS, BUFFER_RESOLUTION_MS);
Timer bufferTimer(BUFFER_RESOLUTION_MS);
Timer logTimer(LOG_INTERVAL_MS);

Format heightFormat("###.# cm", Format::Alignment::RIGHT);

constexpr uint16_t DISPLAY_HEIGHT = 135;
constexpr uint16_t DISPLAY_WIDTH = 240;
constexpr uint16_t HEADER_HEIGHT = 3 * 8 + 4; // one line of text size 3 plus padding

Color LakeBlue = Color565::fromRGB(0, 0, 255);
constexpr RangeF ROLLING_RANGE = { 0, 40 };
constexpr Rect16 ROLLING_RECT(0, HEADER_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT);
RollingBarChart rollingChart(ROLLING_RECT, ROLLING_RANGE, LakeBlue, Color::BLACK);

void setup()
{
   SerialX::begin();
   arduino.begin();

   // Connect to WiFi
   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

   arduino.setTextSize(2);
   arduino.setCursorY(-arduino.charH());
	arduino.println("Wave Subscriber", Color::GRAY);

	arduino.println("Initializing", Color::HEADING2);
	arduino.moveCursorY(4);

	arduino.print("WiFi...", Color::LABEL);
	while (WiFi.status() != WL_CONNECTED)
	{
		arduino.print(".", Color::LABEL);
	}
	arduino.printlnR("OK", Color::VALUE);
	arduino.moveCursorY(1);

   arduino.print("WebSocket...", Color::LABEL);

   client.setCallbacks(nullptr, onDisconnected, nullptr, nullptr, onError, onStarted);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);

   delay(1000); // provide time for the wind meter to get a reading
}

void onStarted()
{
   arduino.clearDisplay();
}

void onError(std::string msg)
{
   arduino.setTextSize(2);
   arduino.clearDisplay();
   arduino.display.setTextWrap(true);
   arduino.println(msg, Color::WHITE, Color::RED);
   Util::reset(10);
}

void onDisconnected()
{
   Util::reset();
}

float lastDelta = 0;

void loop()
{
	client.loop();

	if (client.isStarted() == false)
	{
		return;
	}

	// get value measured from the bottom of the graph
	float sensorReading = client.getValue();

	if (bufferTimer.ready())
	{
		if (isnan(sensorReading))
		{
			return;
		}

		sensorReadings.set(sensorReading);
		float avgSensorReading = sensorReadings.get();

		float delta = avgSensorReading - sensorReading;
		if (fabs(delta - lastDelta) < 5)
		{
			waveHeight.set(delta);
			lastDelta = delta;
		}
	}

	if (waveHeight.ready() == false)
	{
		return;
	}

	rollingChart.set((ROLLING_RANGE.min + ROLLING_RANGE.max) / 2.0 + waveHeight.get());

	// display values
	arduino.setCursor(0, 0);
	arduino.setTextSize(3);

	arduino.setTextSize(2);
	arduino.print(client.getTopic(), Color::HEADING);
	arduino.setTextSize(3);
	arduino.printR(waveHeight.get(), heightFormat, Color::VALUE);
	arduino.moveCursorY(4);

	refreshRate.tick();
	rollingChart.draw(&arduino.display);

	if (logTimer.ready())
	{
		Serial.println(String("Rate: ") + String(refreshRate.get()) + " data pts per sec");
		Serial.println(String("Sensor Reading: ") + String(sensorReading));
		Serial.println(String("Wave Height: ") + String(waveHeight.get()));
	}
}
