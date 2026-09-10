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
#include "Timer.h"
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
Format leftAzimuthFormat("###", Format::Alignment::LEFT);
Format rightAzimuthFormat("###", Format::Alignment::RIGHT);
int16_t lineLength = 0;
int16_t gateOriginY = 0;

// ----------- Last open time (updated whenever the gate transitions from closed to
// open; 0 until the gate has opened at least once since boot)
time_t lastGateOpenTime = 0;

// ----------- Tap detection for the "Last Open" footer text, which opens the history
// view when tapped. Rect is only valid (and the footer only tappable) once
// lastOpenFooterVisible is true, i.e. after the gate has opened at least once.
bool lastOpenFooterVisible = false;
Rect16 lastOpenFooterRect;

constexpr auto PREFERENCES_NAMESPACE = SKETCH_NAME;
constexpr auto HISTORY_PREFERENCES_KEY = "history";
constexpr size_t GATE_OPEN_HISTORY_SIZE = 10;

///
/// <summary>
/// One recorded gate-opening event: the time it happened.
/// </summary>
///
struct GateOpenRecord
{
   time_t time;
};

///
/// <summary>
/// Persists the most recent GATE_OPEN_HISTORY_SIZE gate-opening events (combined across
/// both gates) to Preferences (NVS) as a fixed-size blob, newest entry first. Held
/// entirely in RAM between load() and save() calls; save() is only called when a new
/// event is appended, since opens are infrequent.
/// </summary>
///
class GateOpenHistory
{
private:
   GateOpenRecord _records[GATE_OPEN_HISTORY_SIZE] = {};
   size_t _count = 0;

public:
   ///
   /// <summary>
   /// Loads the saved history from Preferences, if present, discarding any entries with
   /// an implausible timestamp (e.g. recorded before the clock had synced, which would
   /// otherwise show up as an opening on Dec 31st 1969). Leaves the history empty if no
   /// saved data exists yet.
   /// </summary>
   ///
   void load()
   {
      arduino.preferences.begin(PREFERENCES_NAMESPACE, true);

      size_t savedSize = arduino.preferences.getBytesLength(HISTORY_PREFERENCES_KEY);
      if (savedSize > 0 && savedSize <= sizeof(_records))
      {
         arduino.preferences.getBytes(HISTORY_PREFERENCES_KEY, _records, savedSize);
         _count = savedSize / sizeof(GateOpenRecord);
      }

      arduino.preferences.end();

      // Constant duplicated from TimeSync::isSynced() rather than depending on TimeSync
      // here, since a synced clock is what distinguishes a real timestamp from bogus
      // pre-sync data.
      constexpr time_t MIN_VALID_TIME = 1000000000l;

      size_t validCount = 0;
      for (size_t i = 0; i < _count; i++)
      {
         if (_records[i].time >= MIN_VALID_TIME)
         {
            _records[validCount++] = _records[i];
         }
      }

      if (validCount != _count)
      {
         _count = validCount;

         arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
         arduino.preferences.putBytes(HISTORY_PREFERENCES_KEY, _records, _count * sizeof(GateOpenRecord));
         arduino.preferences.end();
      }
   }

   ///
   /// <summary>
   /// Adds a new gate-opening event as the newest entry, shifting older entries back
   /// (dropping the oldest if the history is already full), and persists the updated
   /// history to Preferences.
   /// </summary>
   /// <param name="time">Time the gate opened.</param>
   ///
   void add(time_t time)
   {
      size_t newCount = (_count < GATE_OPEN_HISTORY_SIZE) ? (_count + 1) : GATE_OPEN_HISTORY_SIZE;
      for (size_t i = newCount - 1; i > 0; i--)
      {
         _records[i] = _records[i - 1];
      }
      _records[0] = { time };
      _count = newCount;

      arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
      arduino.preferences.putBytes(HISTORY_PREFERENCES_KEY, _records, _count * sizeof(GateOpenRecord));
      arduino.preferences.end();
   }

   ///
   /// <summary>Gets the number of recorded events, from 0 up to GATE_OPEN_HISTORY_SIZE.</summary>
   /// <returns>Recorded event count.</returns>
   ///
   size_t count() const
   {
      return _count;
   }

   ///
   /// <summary>Gets a recorded event, with index 0 being the most recent.</summary>
   /// <param name="index">Index from 0 (most recent) to count() - 1 (oldest).</param>
   /// <returns>The recorded event at the given index.</returns>
   ///
   const GateOpenRecord& get(size_t index) const
   {
      return _records[index];
   }
};

GateOpenHistory gateOpenHistory;

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
/// Formats a time_t as a friendly date string, e.g. "Aug 12th", using a 3-letter
/// month abbreviation and an ordinal day suffix (st/nd/rd/th).
/// </summary>
/// <param name="time">Time to format.</param>
/// <returns>Friendly date string, e.g. "Aug 12th".</returns>
///
std::string formatFriendlyDate(time_t time)
{
   static constexpr const char* MONTH_NAMES[12] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
   };

   struct tm timeInfo;
   localtime_r(&time, &timeInfo);

   int day = timeInfo.tm_mday;
   const char* suffix;
   if (day % 10 == 1 && day != 11)
   {
      suffix = "st";
   }
   else if (day % 10 == 2 && day != 12)
   {
      suffix = "nd";
   }
   else if (day % 10 == 3 && day != 13)
   {
      suffix = "rd";
   }
   else
   {
      suffix = "th";
   }

   return std::string(MONTH_NAMES[timeInfo.tm_mon]) + " " + std::to_string(day) + suffix;
}

///
/// <summary>
/// Background/banner color used to indicate an open gate, halfway between orange and
/// yellow.
/// </summary>
///
constexpr Color GATE_OPEN_COLOR = (Color)Color565::fromRGB(255, 210, 0);

///
/// <summary>
/// Draws both gates' azimuth values, left and right aligned respectively, inline with
/// the origin circles, with no decimals and a degree symbol, and (once the gate has
/// opened at least once since boot) the last time the gate was opened, shown as footer
/// text centered at the bottom of the display. The background is black while closed
/// and matches the gate-open banner color while open; the text is gray while closed
/// and dark orange while open.
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
   Color textColor = isOpen ? Color::DARKORANGE : Color::GRAY;

   // Draw the azimuth values inline with the origin circles rather than at the very
   // bottom of the display.
   int16_t azimuthY = gateOriginY - arduino.charH() / 2;

   arduino.setCursor(0, azimuthY);
   arduino.print(leftAzimuth, leftAzimuthFormat, textColor, backgroundColor);

   arduino.setCursor(arduino.width(), azimuthY);
   arduino.printR(rightAzimuth, rightAzimuthFormat, textColor, backgroundColor);

   lastOpenFooterVisible = (lastGateOpenTime != 0);
   if (lastOpenFooterVisible)
   {
      struct tm timeInfo;
      localtime_r(&lastGateOpenTime, &timeInfo);

      char timeBuffer[16];
      strftime(timeBuffer, sizeof(timeBuffer), "%I:%M %p", &timeInfo);
      const char* timeStr = (timeBuffer[0] == '0') ? timeBuffer + 1 : timeBuffer;

      std::string dateStr = formatFriendlyDate(lastGateOpenTime);

      std::string lastOpenText = std::string("Last Open: ") + timeStr + ", " + dateStr;

      arduino.setCursorY(-arduino.charH());

      // Tap target is at least double the text's height, extending equally above and
      // below it, to make it easier to tap without needing to make the text itself larger.
      int16_t tapMargin = arduino.charH() / 2;
      lastOpenFooterRect = Rect16(0, arduino.getCursor().y - tapMargin, arduino.width(), arduino.charH() + 2 * tapMargin);

      arduino.printC(lastOpenText.c_str(), textColor, backgroundColor);
   }

   arduino.setTextSize(savedTextSize);
   arduino.setCursor(savedCursor);
}

// ----------- History view (shown when the "Last Open" footer is tapped)
constexpr uint16_t HISTORY_VIEW_TIMEOUT_S = 15;
constexpr uint8_t HISTORY_TITLE_TEXT_SIZE = 4;
constexpr uint8_t HISTORY_ROW_TEXT_SIZE = 2;

///
/// <summary>
/// Draws a full-screen list of up to GATE_OPEN_HISTORY_SIZE recorded gate-opening
/// events, most recent first, each showing the friendly date, time, and which gate
/// opened. Drawn once; the caller is responsible for returning to the main view.
/// </summary>
///
void displayHistoryView()
{
   arduino.clearDisplay();

   arduino.setTextSize(HISTORY_TITLE_TEXT_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("Gate History", Color::HEADING);

   arduino.setTextSize(HISTORY_ROW_TEXT_SIZE);

   if (gateOpenHistory.count() == 0)
   {
      arduino.println("No openings recorded", Color::GRAY);
   }
   else
   {
      for (size_t i = 0; i < gateOpenHistory.count(); i++)
      {
         const GateOpenRecord& record = gateOpenHistory.get(i);

         struct tm timeInfo;
         localtime_r(&record.time, &timeInfo);

         char timeBuffer[16];
         strftime(timeBuffer, sizeof(timeBuffer), "%I:%M %p", &timeInfo);
         const char* timeStr = (timeBuffer[0] == '0') ? timeBuffer + 1 : timeBuffer;

         std::string dateStr = formatFriendlyDate(record.time);
         std::string rowText = dateStr + " " + timeStr;

         arduino.println(rowText.c_str(), Color::GRAY);
      }
   }

   arduino.setTextSize(HISTORY_ROW_TEXT_SIZE);
   arduino.setCursor(0, -arduino.charH(HISTORY_ROW_TEXT_SIZE));
   arduino.print("Tap to return", Color::GRAY);
}

constexpr int16_t GATE_STATE_TOP_MARGIN = 10;
constexpr int16_t GATE_STATE_BOTTOM_MARGIN = 7;

///
/// <summary>
/// Draws the overall gate state ("CLOSED" or "OPEN") centered at the top of the
/// display in size 5 text, with its background filling the full display width and a
/// 10px top margin and 7px bottom margin. Closed is shown in gray text on a black
/// background, with a "Tap to open" hint below it in size 2 gray text; open is shown
/// in black text on an orange background. The firmware version is drawn in size 2
/// text in the lower right corner, matching the state text color.
/// </summary>
/// <param name="isOpen">True if either gate's azimuth is greater than 10 degrees; false if both gates are at or below that threshold.</param>
/// <param name="forceRedraw">If true, redraws even if isOpen hasn't changed since the last call (e.g. after returning from the history view).</param>
///
void displayGateState(bool isOpen, bool forceRedraw = false)
{
   static bool lastIsOpen = false;
   static bool everDrawn = false;

   if (everDrawn && !forceRedraw && isOpen == lastIsOpen)
   {
      return;
   }

   lastIsOpen = isOpen;
   everDrawn = true;

   Point16 savedCursor = arduino.getCursor();
   uint8_t savedTextSize = arduino.getTextSize();

   arduino.setTextSize(5);

   Color backgroundColor = isOpen ? GATE_OPEN_COLOR : Color::BLACK;
   Color textColor = isOpen ? Color::BLACK : Color::GRAY;
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

   arduino.setTextSize(2);
   arduino.setCursor(arduino.width(), arduino.height() - arduino.charH());
   arduino.printR(VERSION, textColor, backgroundColor);

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

      // Started only after the left client's SSL handshake completes, rather than
      // alongside it in setup(), so the two TLS handshakes don't run concurrently and
      // risk starving the task watchdog.
      rightClient.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);
   }
};

GateTelemetryHandler telemetryHandler(&arduino.status);

void setup()
{
   SerialX::begin();
   arduino.begin();

   gateOpenHistory.load();

   if (gateOpenHistory.count() > 0)
   {
      lastGateOpenTime = gateOpenHistory.get(0).time;
   }

   leftClient.setHandler(&telemetryHandler);

   arduino.beginInit();

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &arduino.status);

   arduino.enableOTA(VERSION, SKETCH_NAME);

   arduino.initClient("Telemetry", []() { leftClient.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &arduino.status);
}

void loop()
{
   arduino.checkForOTA();

   leftClient.loop();
   rightClient.loop();

   if (leftClient.isStarted() == false)
   {
      return;
   }

   lgfx::touch_point_t touchPoint;
   bool touched = arduino.display.getTouch(&touchPoint) > 0;

   static bool showingHistory = false;
   static bool wasTouched = false;
   static TimerSecs historyTimeoutTimer(HISTORY_VIEW_TIMEOUT_S);

   bool tapped = touched && !wasTouched;
   wasTouched = touched;

   bool forceRedraw = false;
   if (showingHistory)
   {
      if (tapped || historyTimeoutTimer.ready())
      {
         showingHistory = false;
         arduino.clearDisplay();
         forceRedraw = true;
      }
      else
      {
         return;
      }
   }

   float leftAzimuth = leftClient.getValue();
   float rightAzimuth = rightClient.isStarted() ? rightClient.getValue() : NAN;

   constexpr float GATE_OPEN_THRESHOLD_DEGREES = 5.0f;
   bool isOpen = (!isnan(leftAzimuth) && leftAzimuth > GATE_OPEN_THRESHOLD_DEGREES) || (!isnan(rightAzimuth) && rightAzimuth > GATE_OPEN_THRESHOLD_DEGREES);

   static bool lastIsOpen = false;
   static bool everDrawn = false;
   if (!everDrawn || isOpen != lastIsOpen || forceRedraw)
   {
      // the background was just repainted for the new state, so force both lines to redraw
      // in the correct color even if their azimuth hasn't changed
      leftLine.lastAzimuth = NAN;
      rightLine.lastAzimuth = NAN;

      if (isOpen && !lastIsOpen && TimeSync::isSynced())
      {
         lastGateOpenTime = time(nullptr);

         gateOpenHistory.add(lastGateOpenTime);
      }

      lastIsOpen = isOpen;
      everDrawn = true;
   }

   displayGateState(isOpen, forceRedraw);

   displayFooterAzimuths(leftAzimuth, rightAzimuth, isOpen);

   if (tapped && lastOpenFooterVisible &&
       touchPoint.x >= lastOpenFooterRect.left() && touchPoint.x < lastOpenFooterRect.right() &&
       touchPoint.y >= lastOpenFooterRect.top() && touchPoint.y < lastOpenFooterRect.bottom())
   {
      showingHistory = true;
      historyTimeoutTimer.reset();
      displayHistoryView();
      return;
   }

   if (!isnan(leftAzimuth))
   {
      displayLine(leftLine, leftAzimuth, isOpen);
   }

   if (!isnan(rightAzimuth))
   {
      displayLine(rightLine, rightAzimuth, isOpen);
   }
}
