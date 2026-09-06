//
// Gate Subscriber Display
//
// Subscribes to live gate azimuth telemetry over a WebSocket connection and renders it
// as a line anchored at the lower-left corner of the display, with the line's angle
// matching the received azimuth value (0-360 degrees) and a fixed length of half the
// display height.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   receives live azimuth readings as they arrive.
// - Redraws the line whenever a new value is received.
// - Resets the device on telemetry disconnect or error.
// - Checks for a firmware update periodically.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

constexpr auto TELEMETRY_TOPIC = "Gate/Left";

constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Gate_Subscriber_Display";

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_BUILTIN_LED_SUPPORTED
#error "This sketch requires a board with a separate built-in LED (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include <cmath>

#include "SerialX.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "WiFiSettings.h"

// ----------- Telemetry
Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);

// ----------- Built-in LED (flashes on each received telemetry value)
constexpr uint16_t RECEIVE_LED_FLASH_MS = 20;

// ----------- Line geometry (anchored at the lower-left corner)
constexpr int16_t LINE_START_X = 0;
Format azimuthFormat("###.#", Format::Alignment::RIGHT);

///
/// <summary>
/// Draws the sketch's title header (the telemetry topic) at the top of the display.
/// </summary>
///
void displayHeader()
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println(TELEMETRY_TOPIC, Color::HEADING);
}

///
/// <summary>
/// Draws a subheading below the header indicating whether the telemetry server is
/// local or remote.
/// </summary>
///
void displaySubheading()
{
   arduino.setTextSize(2);
#ifdef TELEMETRY_LOCAL
   arduino.println("Local", Color::SUB_HEADING);
#else
   arduino.println("Remote", Color::SUB_HEADING);
#endif
}

///
/// <summary>
/// Draws the server mode (Local/Remote) and telemetry topic as footer text at the
/// bottom of the display, left and right aligned respectively. Only shown on the
/// setup screen; it's cleared when the main display is drawn on telemetry start.
/// </summary>
///
void displayFooter()
{
   Point16 savedCursor = arduino.getCursor();
   uint8_t savedTextSize = arduino.getTextSize();

   arduino.setTextSize(2);

   arduino.setCursor(0, -arduino.charH());
#ifdef TELEMETRY_LOCAL
   arduino.print("Local", Color::GRAY);
#else
   arduino.print("Remote", Color::GRAY);
#endif

   arduino.setCursor(arduino.width(), -arduino.charH());
   arduino.printR(TELEMETRY_TOPIC, Color::GRAY);

   arduino.setTextSize(savedTextSize);
   arduino.setCursor(savedCursor);
}

///
/// <summary>
/// Draws the azimuth line anchored at the lower-left corner of the display, with a
/// length of half the display height and an angle matching the given azimuth.
/// </summary>
/// <param name="azimuth">Compass heading in degrees (0-360), 0 pointing up</param>
///
void displayLine(float azimuth)
{
   int16_t lineLength = (int16_t)arduino.height() / 2;
   int16_t startY = (int16_t)arduino.height() - 1;

   float azimuthRad = azimuth * (float)M_PI / 180.0f;
   int16_t endX = LINE_START_X + (int16_t)(lineLength * sin(azimuthRad));
   int16_t endY = startY - (int16_t)(lineLength * cos(azimuthRad));

   arduino.drawLine(LINE_START_X, startY, endX, endY, Color::WHITE);
}

TelemetrySubscriber client(TELEMETRY_TOPIC, &status);

///
/// <summary>
/// Handles telemetry lifecycle events for this sketch: draws the header once started
/// and redraws the azimuth line on each received value. Disconnect and error handling
/// use the base class's default behavior.
/// </summary>
///
class GateTelemetryHandler : public TelemetryEventHandler
{
public:
   explicit GateTelemetryHandler(IStatus* status) : TelemetryEventHandler(status, &arduino)
   {
   }

   void onStarted() override
   {
      TelemetryEventHandler::onStarted();

      arduino.clearDisplay();
      displayHeader();
      displaySubheading();
   }

   void onReceiveText(const std::string& text) override
   {
      (void)text;

      // briefly flash the built-in LED to indicate a new value was received
      arduino.led.flash(RECEIVE_LED_FLASH_MS);
   }
};

GateTelemetryHandler telemetryHandler(&status);

void setup()
{
   SerialX::begin();
   arduino.begin();
   status.begin();

   client.setHandler(&telemetryHandler);

   arduino.beginInit();
   displayFooter();

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &status);

   arduino.enableOTA(VERSION, SKETCH_NAME);

   arduino.initClient("WebSocket", []() { client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &status);
}

void loop()
{
   arduino.loop();

   client.loop();

   if (client.isStarted() == false)
   {
      return;
   }

   float azimuth = client.getValue();

   arduino.setTextSize(3);
   arduino.setCursor(0, arduino.charH(3) + 4);
   arduino.printlnR(azimuth, azimuthFormat, Color::VALUE);

   if (!isnan(azimuth))
   {
      arduino.clear(Rect16(0, arduino.charH(3) * 2 + 8, arduino.width(), arduino.height() - (arduino.charH(3) * 2 + 8)));
      displayLine(azimuth);
   }
}
