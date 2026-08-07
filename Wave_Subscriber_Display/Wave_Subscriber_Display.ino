//
// Wave Subscriber Display
//
// Subscribes to live lake wave-height telemetry over a WebSocket connection and renders
// a smoothed rolling bar chart on the display.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   receives live wave sensor readings as they arrive.
// - Applies short-term smoothing/buffering before updating the displayed value and chart.
// - Resets the device on telemetry disconnect or error.
//

// Undefine to use the remote server.
//#define TELEMETRY_LOCAL

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_LED_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Feather ESP32-S3 or Waveshare ESP32-S3-Zero)."
#endif

#include "BarChart.h"
#include "MovingBarChart.h"
#include "BufferedTimeSeries.h"
#include "RollingRate.h"
#include "RollingStats.h"
#include "SerialX.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "Util.h"
#include "WiFiSettings.h"

// ----------- Telemetry
Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);
RollingRate refreshRate(100);
TelemetrySubscriber client("Waves/Lake");
RollingStats sensorReadings(500);

// ----------- Buffering / smoothing
constexpr unsigned long LOG_INTERVAL_MS = 1000;
constexpr unsigned long BUFFER_TIME_SPAN_MS = 2000;
constexpr unsigned long BUFFER_RESOLUTION_MS = 100;

BufferedTimeSeries waveHeight(BUFFER_TIME_SPAN_MS, BUFFER_RESOLUTION_MS);
Timer bufferTimer(BUFFER_RESOLUTION_MS);
Timer logTimer(LOG_INTERVAL_MS);

Format heightFormat("###.# cm", Format::Alignment::RIGHT);

// ----------- Display layout
constexpr uint16_t DISPLAY_HEIGHT = 135;
constexpr uint16_t DISPLAY_WIDTH = 240;
constexpr uint16_t HEADER_HEIGHT = 3 * 8 + 4; // one line of text size 3 plus padding

// ----------- Rolling bar chart view
Color LakeBlue = Color565::fromRGB(0, 0, 255);
constexpr RangeF ROLLING_RANGE = { 0, 40 };
constexpr Rect16 ROLLING_RECT(0, HEADER_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT);
MovingBarChart rollingChart(ROLLING_RECT, ROLLING_RANGE, LakeBlue, Color::BLACK);

void setup()
{
   SerialX::begin();
   arduino.begin();
   status.begin();
   status.setStatus(Status::STARTED);

   arduino.printHeader("Initializing");

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &status);

   client.setCallbacks(nullptr, onDisconnected, nullptr, nullptr, onError, onStarted);
   arduino.beginClient("WebSocket", []() { client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &status);

   delay(1000); // provide time for the wave sensor to get a reading
}

void onStarted()
{
   status.setStatus(Status::READY);
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

void onDisconnected(std::string reason)
{
   Serial.println("Disconnected: " + String(reason.c_str()));
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
