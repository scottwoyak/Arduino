//
// Gate Viewer
//
// Subscribes to live gate azimuth telemetry over WebSocket connections and renders both
// the left and right gate angles on the Hosyond ESP32-32E 4" display (Viewer board) as
// lines anchored at the lower corners of the display, with each line's angle matching
// its received azimuth value (0-360 degrees) and a fixed length of half the display
// height. The telemetry server does not yet support subscribing to multiple topics on
// a single connection, so a second, minimal WebSocket client is used here just for the
// right gate topic instead of a second TelemetrySubscriber instance.
//
// Behavior:
// - Connects to WiFi, then opens two WebSocket connections to the telemetry server (one
//   per gate topic) and receives live azimuth readings as they arrive.
// - Redraws each line whenever a new value is received for its gate.
// - Resets the device on telemetry disconnect or error.
// - Checks for a firmware update periodically.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

constexpr auto LEFT_TELEMETRY_TOPIC = "Gate/Left";
constexpr auto RIGHT_TELEMETRY_TOPIC = "Gate/Right";

constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Gate_Viewer";

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Viewer)."
#endif

#include <cmath>

#include <WebSocketsClient.h>

#include "SerialX.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "TimeSync.h"
#include "WiFiSettings.h"

// ----------- Telemetry
Arduino arduino;

// ----------- Line geometry (left line anchored 50px from the left edge, right line
// anchored 50px from the right edge of the display; the gate origin's Y position is
// the display's bottom edge minus the origin margin. Line length is fixed at half the
// distance between the two origins, so both lines meet exactly when closed, i.e. when
// both gates report an azimuth of 0. A circle is drawn around each gate's origin, and
// the portion of the line inside that circle is not drawn.)
constexpr int16_t GATE_ORIGIN_MARGIN = 50;
constexpr int16_t GATE_ORIGIN_RADIUS = 10;
Format azimuthFormat("###", Format::Alignment::RIGHT);
int16_t lineLength = 0;
int16_t gateOriginY = 0;

// ----------- Last open time (updated whenever the gate transitions from closed to
// open; 0 until the gate has opened at least once since boot)
time_t lastGateOpenTime = 0;

///
/// <summary>
/// Tracks the previously drawn endpoint of one gate's azimuth line so it can be erased
/// (redrawn in black) before the new angle is drawn, and avoids redrawing entirely when
/// the azimuth hasn't changed.
/// </summary>
///
struct LineState
{
   int16_t startX;
   int16_t lastStartX;
   int16_t lastStartY = 0;
   int16_t lastEndX;
   int16_t lastEndY = 0;
   bool lineDrawn = false;
   float lastAzimuth = NAN;
   bool mirrorX = false;
};

LineState leftLine{ 0 };
LineState rightLine{ 0, 0, 0, 0, 0, false, NAN, true };

///
/// <summary>
constexpr Color GATE_OPEN_COLOR = (Color)Color565::fromRGB(255, 210, 0); // halfway between orange (255,165,0) and yellow (255,255,0)

///
/// <summary>
/// Draws both gates' azimuth values as footer text at the bottom of the display, left
/// and right aligned respectively, in gray with no decimals and a degree symbol, and
/// (once the gate has opened at least once since boot) the last time the gate was
/// opened, centered between them. The background is black while closed and matches
/// the gate-open banner color while open.
/// </summary>
/// <param name="leftAzimuth">Left gate's azimuth in degrees, or NAN if unavailable.</param>
/// <param name="rightAzimuth">Right gate's azimuth in degrees, or NAN if unavailable.</param>
/// <param name="isOpen">True if the gate is currently open; false if closed.</param>
///
void displayFooterAzimuths(float leftAzimuth, float rightAzimuth, bool isOpen)
{
   Point16 savedCursor = arduino.getCursor();
   uint8_t savedTextSize = arduino.getTextSize();

   arduino.setTextSize(2);

   Color backgroundColor = isOpen ? GATE_OPEN_COLOR : Color::BLACK;

   arduino.setCursor(0, -arduino.charH());
   arduino.print(leftAzimuth, azimuthFormat, Color::GRAY, backgroundColor);

   arduino.setCursor(arduino.width(), -arduino.charH());
   arduino.printR(rightAzimuth, azimuthFormat, Color::GRAY, backgroundColor);

   if (lastGateOpenTime != 0)
   {
      struct tm timeInfo;
      localtime_r(&lastGateOpenTime, &timeInfo);

      char timeBuffer[16];
      strftime(timeBuffer, sizeof(timeBuffer), "%I:%M %p", &timeInfo);
      const char* timeStr = (timeBuffer[0] == '0') ? timeBuffer + 1 : timeBuffer;

      char dateBuffer[16];
      strftime(dateBuffer, sizeof(dateBuffer), "%m/%d", &timeInfo);
      const char* dateStr = (dateBuffer[0] == '0') ? dateBuffer + 1 : dateBuffer;

      std::string lastOpenText = std::string("Last Open: ") + timeStr + " " + dateStr;

      arduino.setCursorY(-arduino.charH());
      arduino.printC(lastOpenText.c_str(), Color::GRAY, backgroundColor);
   }

   arduino.setTextSize(savedTextSize);
   arduino.setCursor(savedCursor);
}

constexpr int16_t GATE_STATE_TOP_MARGIN = 10;
constexpr int16_t GATE_STATE_BOTTOM_MARGIN = 7;

///
/// <summary>
/// Draws the overall gate state ("CLOSED" or "OPEN") centered at the top of the
/// display in size 5 text, with its background filling the full display width and a
/// 10px top margin and 7px bottom margin. Closed is shown in gray text on a black
/// background, with a "Tap to open" hint below it in size 2 gray text; open is shown
/// in black text on an orange background.
/// </summary>
/// <param name="isOpen">True if either gate's azimuth is greater than 10 degrees; false if both gates are at or below that threshold.</param>
///
void displayGateState(bool isOpen)
{
   static bool lastIsOpen = false;
   static bool everDrawn = false;

   if (everDrawn && isOpen == lastIsOpen)
   {
      return;
   }

   lastIsOpen = isOpen;
   everDrawn = true;

   Point16 savedCursor = arduino.getCursor();
   uint8_t savedTextSize = arduino.getTextSize();

   arduino.setTextSize(5);

   Color backgroundColor = isOpen ? GATE_OPEN_COLOR : Color::BLACK;
   int16_t rowHeight = GATE_STATE_TOP_MARGIN + arduino.charH(5) + GATE_STATE_BOTTOM_MARGIN + arduino.charH(2);
   arduino.fillRect(0, 0, arduino.width(), arduino.height(), backgroundColor);

   if (isOpen)
   {
      arduino.setCursor(0, (rowHeight - arduino.charH(5)) / 2);
      arduino.printlnC("OPEN", Color::BLACK, GATE_OPEN_COLOR);
   }
   else
   {
      arduino.setCursor(0, GATE_STATE_TOP_MARGIN);
      arduino.printlnC("CLOSED", Color::GRAY, Color::BLACK);

      arduino.setTextSize(2);
      arduino.moveCursorY(-4);
      arduino.printlnC("Tap to open", Color::GRAY, Color::BLACK);
   }

   arduino.setTextSize(savedTextSize);
   arduino.setCursor(savedCursor);
}

///
/// <summary>
/// Draws a gate's azimuth line anchored at its origin below the value text, with a
/// fixed length (see lineLength) and an angle matching the given azimuth. Only
/// redraws (erasing the previous line first) when the azimuth has actually changed.
/// The line and origin circle are drawn in black while the gate is open (orange
/// background) and in white while closed (black background).
/// </summary>
/// <param name="line">Per-gate line state to read/update.</param>
/// <param name="azimuth">Angle in degrees (0-360); for non-mirrored lines 0 points right and
/// 90 points up, while mirrored lines (see LineState::mirrorX) point left at 0 and still up
/// at 90.</param>
/// <param name="isOpen">True if the gate is currently open (orange background); false if closed (black background).</param>
///
void displayLine(LineState& line, float azimuth, bool isOpen)
{
   if (azimuth == line.lastAzimuth)
   {
      return;
   }

   Color lineColor = isOpen ? Color::BLACK : Color::WHITE;
   Color eraseColor = isOpen ? GATE_OPEN_COLOR : Color::BLACK;

   float azimuthRad = azimuth * (float)M_PI / 180.0f;
   float xDir = line.mirrorX ? -cos(azimuthRad) : cos(azimuthRad);
   float yDir = sin(azimuthRad);
   int16_t startX = line.startX + (int16_t)lround(GATE_ORIGIN_RADIUS * xDir);
   int16_t startY = gateOriginY - (int16_t)lround(GATE_ORIGIN_RADIUS * yDir);
   int16_t endX = line.startX + (int16_t)lround(lineLength * xDir);
   int16_t endY = gateOriginY - (int16_t)lround(lineLength * yDir);

   if (line.lineDrawn)
   {
      arduino.drawLine(line.lastStartX, line.lastStartY, line.lastEndX, line.lastEndY, eraseColor);
   }

   arduino.drawLine(startX, startY, endX, endY, lineColor);
   arduino.drawCircle(line.startX, gateOriginY, GATE_ORIGIN_RADIUS, lineColor);

   line.lastStartX = startX;
   line.lastStartY = startY;
   line.lastEndX = endX;
   line.lastEndY = endY;
   line.lineDrawn = true;
   line.lastAzimuth = azimuth;
}

TelemetrySubscriber leftClient(LEFT_TELEMETRY_TOPIC, &arduino.status);

///
/// <summary>
/// Minimal, self-contained WebSocket subscriber for the right gate topic. A separate,
/// duplicate client is used here (rather than a second TelemetrySubscriber) because
/// TelemetryClient is built around a single global WebSocketsClient/static instance
/// pointer and only supports one connection at a time; the telemetry server also
/// doesn't yet support subscribing to multiple topics on one connection.
/// </summary>
///
class RightGateSubscriber
{
private:
   WebSocketsClient _webSocket;
   std::string _topic;
   std::string _serverVersion;
   std::string _status;
   bool _started = false;
   float _value = NAN;

   void _sendText(const char* text)
   {
      _webSocket.sendTXT(text);
   }

   void _onEvent(WStype_t type, uint8_t* payload, size_t length)
   {
      switch (type)
      {
      case WStype_DISCONNECTED:
         _serverVersion.clear();
         _status.clear();
         _started = false;
         Serial.println("Right gate disconnected");
         Util::reset(TELEMETRY_RESET_DELAY_S);
         break;

      case WStype_CONNECTED:
         break;

      case WStype_TEXT:
      {
         std::string str((const char*)payload, length);

         if (_serverVersion.length() == 0)
         {
            _serverVersion = str.substr(std::string("TelemetryServer v").length());
         }
         else if (_status.length() == 0)
         {
            _status = str;
            if (str.starts_with("ERR"))
            {
               Serial.print("Right gate start failure: ");
               Serial.println(str.c_str());
            }
            else
            {
               _started = true;
               _sendText("get");
            }
         }
         else
         {
            try
            {
               _value = std::stof(str);
            }
            catch (const std::exception&)
            {
               _value = NAN;
            }

            _sendText("get");
         }
      }
      break;

      default:
         break;
      }
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the RightGateSubscriber class.
   /// </summary>
   /// <param name="topic">The telemetry topic to subscribe to.</param>
   ///
   explicit RightGateSubscriber(const char* topic) : _topic(topic)
   {
   }

   ///
   /// <summary>
   /// Connects to the telemetry server over an encrypted (SSL) WebSocket connection and
   /// sends the subscribe handshake once connected.
   /// </summary>
   /// <param name="host">The server hostname or IP address.</param>
   /// <param name="port">The server port.</param>
   ///
   void beginSSL(const char* host, uint16_t port)
   {
      _webSocket.onEvent([this](WStype_t type, uint8_t* payload, size_t length) { _onEvent(type, payload, length); });
      _webSocket.beginSSL(host, port, "/ws");
   }

   ///
   /// <summary>Gets whether the start/subscribe handshake has completed successfully.</summary>
   /// <returns>True if started; false otherwise.</returns>
   ///
   bool isStarted() const
   {
      return _started;
   }

   ///
   /// <summary>Gets the most recently received value for the subscribed topic.</summary>
   /// <returns>The latest value, or NAN if none has been received yet.</returns>
   ///
   float getValue() const
   {
      return _value;
   }

   ///
   /// <summary>Services the WebSocket connection. Must be called regularly from loop().</summary>
   ///
   void loop()
   {
      _webSocket.loop();

      // sends the subscribe handshake once the connection completes; the base
      // TelemetryClient sends this from its onConnected() callback, but this minimal
      // client keeps its own started/subscribe flag instead of a callback
      static bool subscribeSent = false;
      if (_webSocket.isConnected() && !subscribeSent)
      {
         std::string cmd = "Subscribe " + _topic;
         _sendText(cmd.c_str());
         subscribeSent = true;
      }
      else if (!_webSocket.isConnected())
      {
         subscribeSent = false;
      }
   }
};

RightGateSubscriber rightClient(RIGHT_TELEMETRY_TOPIC);

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

      leftLine = LineState{ GATE_ORIGIN_MARGIN };
      rightLine = LineState{ (int16_t)(arduino.width() - GATE_ORIGIN_MARGIN), 0, 0, 0, 0, false, NAN, true };
      lineLength = (rightLine.startX - leftLine.startX) / 2;
      gateOriginY = arduino.height() - 1 - GATE_ORIGIN_MARGIN;
   }
};

GateTelemetryHandler telemetryHandler(&arduino.status);

void setup()
{
   SerialX::begin();
   arduino.begin();

   leftClient.setHandler(&telemetryHandler);

   arduino.beginInit();

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &arduino.status);

   arduino.enableOTA(VERSION, SKETCH_NAME);

   arduino.initClient("WebSocket", []() { leftClient.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &arduino.status);
   rightClient.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);
}

void loop()
{
   arduino.loop();

   leftClient.loop();
   rightClient.loop();

   if (leftClient.isStarted() == false)
   {
      return;
   }

   float leftAzimuth = leftClient.getValue();
   float rightAzimuth = rightClient.isStarted() ? rightClient.getValue() : NAN;

   constexpr float GATE_OPEN_THRESHOLD_DEGREES = 10.0f;
   bool isOpen = (!isnan(leftAzimuth) && leftAzimuth > GATE_OPEN_THRESHOLD_DEGREES) || (!isnan(rightAzimuth) && rightAzimuth > GATE_OPEN_THRESHOLD_DEGREES);

   static bool lastIsOpen = false;
   static bool everDrawn = false;
   if (!everDrawn || isOpen != lastIsOpen)
   {
      // the background was just repainted for the new state, so force both lines to redraw
      // in the correct color even if their azimuth hasn't changed
      leftLine.lastAzimuth = NAN;
      rightLine.lastAzimuth = NAN;

      if (isOpen && !lastIsOpen && TimeSync::isSynced())
      {
         lastGateOpenTime = time(nullptr);
      }

      lastIsOpen = isOpen;
      everDrawn = true;
   }

   displayGateState(isOpen);

   displayFooterAzimuths(leftAzimuth, rightAzimuth, isOpen);

   if (!isnan(leftAzimuth))
   {
      displayLine(leftLine, leftAzimuth, isOpen);
   }

   if (!isnan(rightAzimuth))
   {
      displayLine(rightLine, rightAzimuth, isOpen);
   }
}
