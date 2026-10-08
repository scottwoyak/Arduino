//
// Gate Viewer
//
// Subscribes to live gate azimuth telemetry over a WebSocket connection and renders both
// the left and right gate angles on a supported display board as lines anchored at the
// lower corners of the display, with each line's angle matching its received azimuth
// value (0-360 degrees) and a fixed length of half the display height. Both gate topics
// are subscribed on a single telemetry connection.
//
// On touch-capable boards (e.g. Viewer, Waveshare ESP32-S3 Touch LCD 4.3), tapping the
// "CLOSED" banner opens the gate, and tapping the "Last Open" footer shows the opening
// history (auto-dismissed after a timeout or on the next tap). Boards without touch
// (e.g. Feather ESP32-S3 TFT) use button A to open the gate instead; the history view
// is unavailable on those boards.
//
// Behavior:
// - Connects to WiFi, then opens a single WebSocket connection to the telemetry server,
//   subscribes to both gate topics, and receives live azimuth readings as they arrive.
// - Redraws each line whenever a new value is received for its gate.
// - If a gate's telemetry topic disconnects or fails to connect, that gate's line is
//   simply left undrawn while the connection keeps retrying in the background; the
//   device resets only if the outage lasts 10 minutes.
// - Checks for a firmware update periodically.
//

#include <cmath>
#include <string>

///
/// <summary>
/// Tracks the previously drawn endpoint of one gate's azimuth line so it can be erased
/// (redrawn in black) before the new angle is drawn, and avoids redrawing entirely when
/// the azimuth hasn't changed.
/// </summary>
/// <remarks>
/// Defined this early (rather than near displayLine()) because the Arduino builder
/// auto-generates function prototypes (e.g. for displayLine()) and inserts them right
/// before the first function definition in the file, which must come after this type
/// is fully defined.
/// </remarks>
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

constexpr auto LEFT_TELEMETRY_TOPIC = "Gate/Left";
constexpr auto RIGHT_TELEMETRY_TOPIC = "Gate/Right";

constexpr auto GATE_OPENER_HOST = "192.168.1.9";
constexpr uint16_t GATE_OPENER_PORT = 80;

#define ARDUINO_HOSYOND_ESP32_S3_VIEWER

// Declares which VLW font sizes this sketch actually uses (2, 4, and 5 - see
// setTextSize()/HISTORY_TITLE_TEXT_SIZE/HISTORY_ROW_TEXT_SIZE below - plus 3, which is
// this board's DEFAULT_HEADING_SIZE used by printInitHeader() during boot), so
// ArduinoWithDisplay.h/Fonts/Roboto*.h only compile in the needed font data instead of
// all 7 sizes, reducing flash usage.
#define TEXT_SIZES_CUSTOM
#define TEXT_SIZE_2
#define TEXT_SIZE_3
#define TEXT_SIZE_4
#define TEXT_SIZE_5

// Only compile in the notification sound this sketch plays (Morning), to save flash.
#define SOUNDS_CUSTOM
#define SOUND_MORNING

#include "ArduinoBoard.h"
#include "LibraryVersion.h"

// This sketch's own version (e.g. "1.06"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("1.1");
constexpr auto SKETCH_NAME = "Gate_Viewer";

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Viewer)."
#include "WrongBoard.h"
#endif

#include <HTTPClient.h>
#include <WebSocketsClient.h>

#include "BufferedTimeSeries.h"
#include "RollingRate.h"
#include "SerialX.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "TimeSync.h"
#include "Timer.h"
#include "WiFiSettings.h"
#include "ViewerSketch.h"

// ----------- Telemetry
SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .enableOTA = true,
};

ViewerSketch sketch(SKETCH_CONFIG);

// ----------- Line geometry (left line anchored 50px from the left edge, right line
// anchored 50px from the right edge of the display; the gate origin's Y position is
// the display's bottom edge minus the origin margin. Line length is fixed at half the
// distance between the two origins, so both lines meet exactly when closed, i.e. when
// both gates report an azimuth of 0. A circle is drawn around each gate's origin, and
// the portion of the line inside that circle is not drawn.)
constexpr int16_t GATE_ORIGIN_MARGIN = 50;

// Radius tuned to look right on the Viewer's 320px-wide display; scaled down
// proportionally for smaller displays (e.g. the Feather's 240px-wide display).
constexpr int16_t GATE_ORIGIN_RADIUS_REFERENCE_WIDTH = 320;
constexpr int16_t GATE_ORIGIN_RADIUS_REFERENCE = 10;
int16_t gateOriginRadius = GATE_ORIGIN_RADIUS_REFERENCE;

Format leftAzimuthFormat("###", Format::Alignment::LEFT);
Format rightAzimuthFormat("###", Format::Alignment::RIGHT);
int16_t lineLength = 0;
int16_t gateOriginY = 0;

// ----------- Azimuth buffering (smooths the gate line animation by interpolating
// between received values rather than snapping to each new reading)
//
// Disabled for now -- it wasn't producing the desired smoothing. Left here (commented
// out) in case it's revisited later.

// // Expected sample spacing (telemetry arrives at ~4 samples/sec on average, but with
// // significant jitter -- gaps of up to ~500ms have been observed), used to size the
// // azimuth buffers' interpolation resolution.
constexpr unsigned long BUFFER_RESOLUTION_MS = 30;
//
// // Duration of history retained in the azimuth buffers. Wide enough to comfortably
// // cover the largest observed gaps between samples (so ready()/get() don't
// // intermittently fail and fall back to the raw, unsmoothed value mid-animation), at
// // the cost of a bit more interpolation lag (half the window).
constexpr unsigned long BUFFER_TIME_SPAN_MS = 100;

BufferedTimeSeries leftAzimuthBuffer(BUFFER_TIME_SPAN_MS, BUFFER_RESOLUTION_MS);
BufferedTimeSeries rightAzimuthBuffer(BUFFER_TIME_SPAN_MS, BUFFER_RESOLUTION_MS);

// ----------- Receive metrics (how fast each gate's telemetry actually arrives)

// How often the receive metrics are printed to the serial log.
constexpr float METRICS_LOG_INTERVAL_S = 5.0f;

// Number of recent samples the receive rate is averaged over.
constexpr uint16_t RATE_WINDOW_SAMPLES = 20;

// A gap between samples longer than this is longer than the azimuth buffer can bridge,
// so the displayed line falls back to the raw, unsmoothed value.
constexpr int32_t BUFFER_GAP_LIMIT_MS = BUFFER_TIME_SPAN_MS / 2;

///
/// <summary>
/// Receive statistics for one gate topic: a rolling sample rate, plus the min/max/average
/// server-reported time between samples and the number of gaps too long for the azimuth
/// buffer to bridge, over the current logging interval.
/// </summary>
///
struct ReceiveMetrics
{
   RollingRate rate{ RATE_WINDOW_SAMPLES };
   uint32_t count = 0;
   uint32_t numLongGaps = 0;
   int32_t minDtMs = INT32_MAX;
   int32_t maxDtMs = 0;
   int64_t totalDtMs = 0;

   ///
   /// <summary>
   /// Records one received sample.
   /// </summary>
   /// <param name="dtMs">Server-reported time since the previous sample, in milliseconds.</param>
   ///
   void record(int32_t dtMs)
   {
      rate.tick();
      count++;
      totalDtMs += dtMs;
      minDtMs = min(minDtMs, dtMs);
      maxDtMs = max(maxDtMs, dtMs);
      if (dtMs > BUFFER_GAP_LIMIT_MS)
      {
         numLongGaps++;
      }
   }

   ///
   /// <summary>
   /// Clears the per-interval statistics (the rolling rate keeps running).
   /// </summary>
   ///
   void resetInterval()
   {
      count = 0;
      numLongGaps = 0;
      minDtMs = INT32_MAX;
      maxDtMs = 0;
      totalDtMs = 0;
   }

   ///
   /// <summary>
   /// Formats the current statistics as a single log line.
   /// </summary>
   /// <param name="name">Label for the gate.</param>
   /// <returns>The formatted line.</returns>
   ///
   String toString(const char* name) const
   {
      if (count == 0)
      {
         return String(name) + ": no samples";
      }

      return String(name) + ": " + String(rate.get(), 1) + "/s, n=" + String(count) +
         ", dt min/avg/max=" + String(minDtMs) + "/" + String((int32_t)(totalDtMs / count)) + "/" + String(maxDtMs) +
         " ms, gaps>" + String(BUFFER_GAP_LIMIT_MS) + "ms=" + String(numLongGaps);
   }
};

ReceiveMetrics leftMetrics;
ReceiveMetrics rightMetrics;
TimerSecs metricsLogTimer(METRICS_LOG_INTERVAL_S);

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
      sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true);

      size_t savedSize = sketch.arduino.preferences.getBytesLength(HISTORY_PREFERENCES_KEY);
      if (savedSize > 0 && savedSize <= sizeof(_records))
      {
         sketch.arduino.preferences.getBytes(HISTORY_PREFERENCES_KEY, _records, savedSize);
         _count = savedSize / sizeof(GateOpenRecord);
      }

      sketch.arduino.preferences.end();

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

         sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
         sketch.arduino.preferences.putBytes(HISTORY_PREFERENCES_KEY, _records, _count * sizeof(GateOpenRecord));
         sketch.arduino.preferences.end();
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

      sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
      sketch.arduino.preferences.putBytes(HISTORY_PREFERENCES_KEY, _records, _count * sizeof(GateOpenRecord));
      sketch.arduino.preferences.end();
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
/// Formats a time_t as a compact numeric date string, e.g. "9/22", for narrower
/// displays that don't have room for formatFriendlyDate()'s longer form.
/// </summary>
/// <param name="time">Time to format.</param>
/// <returns>Compact date string, e.g. "9/22".</returns>
///
std::string formatShortDate(time_t time)
{
   struct tm timeInfo;
   localtime_r(&time, &timeInfo);

   return std::to_string(timeInfo.tm_mon + 1) + "/" + std::to_string(timeInfo.tm_mday);
}

///
/// <summary>
/// Draws both gates' azimuth values, left and right aligned respectively, inline with
/// the origin circles, with no decimals and a degree symbol, and (once the gate has
/// opened at least once since boot) the last time the gate was opened, shown as footer
/// text centered at the bottom of the display. Always drawn in closed-state colors; the
/// open state is shown by inverting the panel.
/// </summary>
/// <param name="leftAzimuth">Left gate's azimuth in degrees, or NAN if unavailable.</param>
/// <param name="rightAzimuth">Right gate's azimuth in degrees, or NAN if unavailable.</param>
///
void displayFooterAzimuths(float leftAzimuth, float rightAzimuth)
{
   Point16 savedCursor = sketch.arduino.getCursor();
   uint8_t savedTextSize = sketch.arduino.getTextSize();

   sketch.arduino.setTextSize(2);

   // Always drawn in closed-state colors; the open state is shown by inverting the panel.
   Color backgroundColor = Color::BLACK;
   Color textColor = Color::DARKGRAY;
   Color messageColor = Color::GRAY;

   // Draw the azimuth values inline with the origin circles rather than at the very
   // bottom of the display.
   int16_t azimuthY = gateOriginY - sketch.arduino.charH() / 2;

   sketch.arduino.setCursor(0, azimuthY);
   sketch.arduino.print(leftAzimuth, leftAzimuthFormat, textColor, backgroundColor);

   sketch.arduino.setCursor(sketch.arduino.width(), azimuthY);
   sketch.arduino.printR(rightAzimuth, rightAzimuthFormat, textColor, backgroundColor);

   lastOpenFooterVisible = (lastGateOpenTime != 0);
   if (lastOpenFooterVisible)
   {
      struct tm timeInfo;
      localtime_r(&lastGateOpenTime, &timeInfo);

      char timeBuffer[16];
      strftime(timeBuffer, sizeof(timeBuffer), "%I:%M %p", &timeInfo);
      const char* timeStr = (timeBuffer[0] == '0') ? timeBuffer + 1 : timeBuffer;

#ifdef ARDUINO_TOUCH_SUPPORTED
      std::string dateStr = formatFriendlyDate(lastGateOpenTime);
      std::string lastOpenText = std::string("Last Open: ") + timeStr + ", " + dateStr;

      sketch.arduino.setCursorX(0);
      sketch.arduino.setCursorY(-sketch.arduino.charH());

      // Tap target is at least double the text's height, extending equally above and
      // below it, to make it easier to tap without needing to make the text itself larger.
      int16_t tapMargin = sketch.arduino.charH() / 2;
      lastOpenFooterRect = Rect16(0, sketch.arduino.getCursor().y - tapMargin, sketch.arduino.width(), sketch.arduino.charH() + 2 * tapMargin);

      sketch.arduino.print(lastOpenText, messageColor, backgroundColor);
#else
      // Narrower, non-touch displays (e.g. the Feather) only have room for a compact
      // date and time, so drop the "Last Open" label, use the short numeric date
      // format, and left-align it instead of centering.
      std::string lastOpenText = formatShortDate(lastGateOpenTime) + ", " + timeStr;

      sketch.arduino.setCursorX(0);
      sketch.arduino.setCursorY(-sketch.arduino.charH());
      sketch.arduino.print(lastOpenText, messageColor, backgroundColor);
#endif
   }

   sketch.arduino.setTextSize(savedTextSize);
   sketch.arduino.setCursor(savedCursor);
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
   sketch.arduino.clearDisplay();

   sketch.arduino.setTextSize(HISTORY_TITLE_TEXT_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Gate History", Color::HEADING);

   sketch.arduino.setTextSize(HISTORY_ROW_TEXT_SIZE);

   if (gateOpenHistory.count() == 0)
   {
      sketch.arduino.println("No openings recorded", Color::GRAY);
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

         sketch.arduino.println(rowText, Color::GRAY);
      }
   }

   sketch.arduino.setTextSize(HISTORY_ROW_TEXT_SIZE);
   sketch.arduino.setCursor(0, -sketch.arduino.charH(HISTORY_ROW_TEXT_SIZE));
   sketch.arduino.print("Tap to return", Color::GRAY);
}

constexpr int16_t GATE_STATE_TOP_MARGIN = 10;
constexpr int16_t GATE_STATE_BOTTOM_MARGIN = 7;

// Non-touch boards (e.g. the Feather's small 240x135 display) use smaller state text so the
// gate lines don't draw over it.
#ifdef ARDUINO_TOUCH_SUPPORTED
constexpr uint8_t GATE_STATE_TEXT_SIZE = 5;
#else
constexpr uint8_t GATE_STATE_TEXT_SIZE = 3;
#endif

// ----------- Tap detection for the gate state banner, which posts an open request when
// tapped while the gate is closed. Rect is only tappable while the gate is closed (the
// banner shows "Tap to open" in that state).
Rect16 gateStateRect;

///
/// <summary>
/// Posts "OPEN" to the Gate_Opener's /Gate endpoint to trigger the gate to open.
/// </summary>
///
void postGateOpen()
{
   HTTPClient http;
   String url = String("http://") + GATE_OPENER_HOST + ":" + GATE_OPENER_PORT + "/Gate";
   Serial.print("Opening gate via ");
   Serial.println(url);
   http.begin(url);
   http.addHeader("Content-Type", "text/plain");
   http.POST("OPEN");
   http.end();
}

///
/// <summary>
/// Draws the overall gate state ("CLOSED" or "OPEN") centered at the top of the
/// display in size 5 text, with its background filling the full display width and a
/// 10px top margin and 7px bottom margin. Closed is shown in gray text on a black
/// background, with a "Tap to open" hint below it in size 2 gray text on touch-capable
/// boards (omitted on display-only boards); open is drawn the same way and shown by
/// inverting the panel. The firmware version is drawn in size 2 text in the lower right corner,
/// matching the state text color.
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

   Point16 savedCursor = sketch.arduino.getCursor();
   uint8_t savedTextSize = sketch.arduino.getTextSize();

   sketch.arduino.setTextSize(GATE_STATE_TEXT_SIZE);

   Color backgroundColor = Color::BLACK;
#ifdef ARDUINO_TOUCH_SUPPORTED
   int16_t rowHeight = GATE_STATE_TOP_MARGIN + sketch.arduino.charH(GATE_STATE_TEXT_SIZE) + GATE_STATE_BOTTOM_MARGIN + sketch.arduino.charH(2);
#else
   int16_t rowHeight = GATE_STATE_TOP_MARGIN + sketch.arduino.charH(GATE_STATE_TEXT_SIZE) + GATE_STATE_BOTTOM_MARGIN;
#endif
   sketch.arduino.fillRect(0, 0, sketch.arduino.width(), sketch.arduino.height(), backgroundColor);

   if (isOpen)
   {
      sketch.arduino.setCursor(0, (rowHeight - sketch.arduino.charH(GATE_STATE_TEXT_SIZE)) / 2);
      sketch.arduino.printlnC("OPEN", Color::GRAY, Color::BLACK);

      gateStateRect = { 0, 0, 0, 0 };
   }
   else
   {
      sketch.arduino.setCursor(0, GATE_STATE_TOP_MARGIN);
      sketch.arduino.printlnC("CLOSED", Color::GRAY, Color::BLACK);

#ifdef ARDUINO_TOUCH_SUPPORTED
      sketch.arduino.setTextSize(2);
      sketch.arduino.moveCursorY(-4);
      sketch.arduino.printlnC("Tap to open", Color::GRAY, Color::BLACK);
#endif

      gateStateRect = { 0, 0, sketch.arduino.width(), (uint16_t)(gateOriginY - sketch.arduino.charH(2) / 2) };
   }

   sketch.arduino.setTextSize(2);
   sketch.arduino.setCursor(sketch.arduino.width(), sketch.arduino.height() - sketch.arduino.charH());
   sketch.arduino.printR(VERSION, Color::DARKGRAY, backgroundColor);

   sketch.arduino.setTextSize(savedTextSize);
   sketch.arduino.setCursor(savedCursor);
}

///
/// <summary>
/// Draws a gate's azimuth line anchored at its origin below the value text, with a
/// fixed length (see lineLength) and an angle matching the given azimuth. Only
/// redraws (erasing the previous line first) when the azimuth has actually changed.
/// The line and origin circle are always drawn in white on black; the open state is
/// shown by inverting the panel.
/// </summary>
/// <param name="line">Per-gate line state to read/update.</param>
/// <param name="azimuth">Angle in degrees (0-360); for non-mirrored lines 0 points right and
/// 90 points up, while mirrored lines (see LineState::mirrorX) point left at 0 and still up
/// at 90.</param>
///
void displayLine(LineState& line, float azimuth)
{
   constexpr float MIN_REDRAW_DEGREES = 0.1f;
   if (!isnan(line.lastAzimuth) && fabsf(azimuth - line.lastAzimuth) <= MIN_REDRAW_DEGREES)
   {
      return;
   }

   Color lineColor = Color::WHITE;
   Color eraseColor = Color::BLACK;

   float azimuthRad = azimuth * (float)M_PI / 180.0f;
   float xDir = line.mirrorX ? -cos(azimuthRad) : cos(azimuthRad);
   float yDir = sin(azimuthRad);
   int16_t startX = line.startX + (int16_t)lround(gateOriginRadius * xDir);
   int16_t startY = gateOriginY - (int16_t)lround(gateOriginRadius * yDir);
   int16_t endX = line.startX + (int16_t)lround(lineLength * xDir);
   int16_t endY = gateOriginY - (int16_t)lround(lineLength * yDir);

   if (line.lineDrawn)
   {
      sketch.arduino.drawLine(line.lastStartX, line.lastStartY, line.lastEndX, line.lastEndY, eraseColor);
   }

   sketch.arduino.drawLine(startX, startY, endX, endY, lineColor);
   sketch.arduino.drawCircle(line.startX, gateOriginY, gateOriginRadius, lineColor);

   line.lastStartX = startX;
   line.lastStartY = startY;
   line.lastEndX = endX;
   line.lastEndY = endY;
   line.lineDrawn = true;
   line.lastAzimuth = azimuth;
}

float leftValue = NAN;
float rightValue = NAN;

///
/// <summary>
/// Handles telemetry lifecycle events for this sketch: draws the header once started
/// and redraws the azimuth line on each received value. Disconnects and failed connections use the base class behavior: the WebSocket retries in the background and the device resets only after the outage lasts TELEMETRY_OUTAGE_RESET_M minutes.
/// </summary>
///
class GateTelemetryHandler : public TelemetryEventHandler
{
private:
   bool _initialized = false;

public:
   // set on the first start; the main loop clears the display, so the
   // "Telemetry... OK" init line printed during setup() is never wiped mid-init
   bool needsInitialClear = false;

   explicit GateTelemetryHandler(IStatus* status) : TelemetryEventHandler(status, &sketch.arduino)
   {
   }

   void onStarted() override
   {
      TelemetryEventHandler::onStarted();

      if (_initialized)
      {
         return;
      }

      _initialized = true;

      needsInitialClear = true;

      leftLine = LineState{ GATE_ORIGIN_MARGIN };
      rightLine = LineState{ (int16_t)(sketch.arduino.width() - GATE_ORIGIN_MARGIN), 0, 0, 0, 0, false, NAN, true };
      lineLength = (rightLine.startX - leftLine.startX) / 2;
      gateOriginY = sketch.arduino.height() - 1 - GATE_ORIGIN_MARGIN;
      gateOriginRadius = (int16_t)lround(GATE_ORIGIN_RADIUS_REFERENCE * (float)sketch.arduino.width() / GATE_ORIGIN_RADIUS_REFERENCE_WIDTH);

      // Subscribe to the right gate topic on the same connection. Only done once
      // (guarded by _initialized above); later reconnects reuse the updated topic list.
      sketch.getClient()->setTopics({ LEFT_TELEMETRY_TOPIC, RIGHT_TELEMETRY_TOPIC });
   }
};

GateTelemetryHandler telemetryHandler(&sketch.arduino.status);

// Set when a Locate request ends, so the next loop() repaints everything.
bool redrawAfterLocate = false;

void setup()
{
   SerialX::begin();

   gateOpenHistory.load();

   if (gateOpenHistory.count() > 0)
   {
      lastGateOpenTime = gateOpenHistory.get(0).time;
   }

   sketch.begin();

#ifdef ARDUINO_SOUND_SUPPORTED
   sketch.arduino.sound.volume = 2.0f;
   sketch.arduino.sound.soundIndex = 2;
#endif

   TelemetrySubscriber* client = sketch.beginTelemetry(LEFT_TELEMETRY_TOPIC, &telemetryHandler);
   client->onSample([](const std::string& topic, double value, int64_t dtMicros)
   {
      if (topic == LEFT_TELEMETRY_TOPIC)
      {
         leftValue = (float)value;
         leftAzimuthBuffer.set(leftValue, dtMicros / 1000);
         leftMetrics.record((int32_t)(dtMicros / 1000));
         Serial.println(String("rx t=") + String(millis()) + " left=" + String(leftValue, 2) + " dt=" + String((int32_t)(dtMicros / 1000)));
      }
      else if (topic == RIGHT_TELEMETRY_TOPIC)
      {
         rightValue = (float)value;
         rightAzimuthBuffer.set(rightValue, dtMicros / 1000);
         rightMetrics.record((int32_t)(dtMicros / 1000));
      }
   });

   sketch.setOnLocateEndCallback([]()
   {
      telemetryHandler.needsInitialClear = true;
      redrawAfterLocate = true;
   });

   sketch.completeInitialization();
}

void loop()
{
   sketch.loop();

   if (sketch.isLocating())
   {
      return;
   }

   if (metricsLogTimer.ready())
   {
      metricsLogTimer.reset();
      Serial.println(leftMetrics.toString("Left rx"));
      Serial.println(rightMetrics.toString("Right rx"));
      leftMetrics.resetInterval();
      rightMetrics.resetInterval();
   }

   TelemetrySubscriber* client = sketch.getClient();

   // Geometry (line endpoints, origins, etc.) is only computed once, the first time
   // the left client starts (see GateTelemetryHandler::onStarted()); until then there's
   // nothing to draw. After that, either topic can independently disconnect/reconnect
   // in the background without resetting the device or blocking the other gate's line
   // from updating (see leftAzimuth/rightAzimuth below).
   if (lineLength == 0)
   {
      return;
   }

   if (telemetryHandler.needsInitialClear)
   {
      telemetryHandler.needsInitialClear = false;
      sketch.arduino.clearDisplay();
   }

   #ifdef ARDUINO_TOUCH_SUPPORTED
   lgfx::touch_point_t touchPoint;
   bool touched = sketch.arduino.display.getTouch(&touchPoint) > 0;

   static bool wasTouched = false;
   bool tapped = touched && !wasTouched;
   wasTouched = touched;
#else
   // No touch hardware on this board: tap-to-open and the history view are unreachable.
   constexpr bool tapped = false;
#endif

   static bool showingHistory = false;
   static TimerSecs historyTimeoutTimer(HISTORY_VIEW_TIMEOUT_S);

   bool forceRedraw = redrawAfterLocate;
   redrawAfterLocate = false;
   if (showingHistory)
   {
      if (tapped || historyTimeoutTimer.ready())
      {
         showingHistory = false;
         sketch.arduino.clearDisplay();
         forceRedraw = true;
      }
      else
      {
         return;
      }
   }

   float leftAzimuth = client->isStarted() ? leftValue : NAN;
   float rightAzimuth = client->isStarted() ? rightValue : NAN;

   // Fall back to the raw value whenever the buffer doesn't yet have enough history to
   // interpolate (e.g. right after startup, or while the gate is stationary and no new
   // samples are arriving), so the line is still drawn instead of disappearing.
   float displayLeftAzimuth = leftAzimuthBuffer.ready() ? leftAzimuthBuffer.get() : leftAzimuth;
   float displayRightAzimuth = rightAzimuthBuffer.ready() ? rightAzimuthBuffer.get() : rightAzimuth;

   // The gate is considered open whenever either displayed angle exceeds the threshold.
   constexpr float GATE_OPEN_THRESHOLD_DEGREES = 3.0f;
   bool isOpen = (!isnan(displayLeftAzimuth) && displayLeftAzimuth > GATE_OPEN_THRESHOLD_DEGREES) ||
                 (!isnan(displayRightAzimuth) && displayRightAzimuth > GATE_OPEN_THRESHOLD_DEGREES);

   static bool lastIsOpen = false;
   static bool everDrawn = false;
   bool stateChanged = !everDrawn || isOpen != lastIsOpen || forceRedraw;
   if (stateChanged)
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

      #ifdef ARDUINO_SOUND_SUPPORTED
      if (isOpen && !lastIsOpen && everDrawn)
      {
         sketch.arduino.sound.playNotificationAsync(2);
      }
      #endif

      lastIsOpen = isOpen;
      everDrawn = true;
   }

   displayGateState(isOpen, forceRedraw);
   displayFooterAzimuths(leftAzimuth, rightAzimuth);

   if (stateChanged)
   {
      sketch.arduino.display.invertDisplay(isOpen);
   }

   #ifdef ARDUINO_TOUCH_SUPPORTED
   if (tapped && lastOpenFooterVisible &&
       touchPoint.x >= lastOpenFooterRect.left() && touchPoint.x < lastOpenFooterRect.right() &&
       touchPoint.y >= lastOpenFooterRect.top() && touchPoint.y < lastOpenFooterRect.bottom())
   {
      showingHistory = true;
      sketch.arduino.display.invertDisplay(false);
      historyTimeoutTimer.reset();
      displayHistoryView();
      return;
   }

   if (tapped && !isOpen &&
       touchPoint.x >= gateStateRect.left() && touchPoint.x < gateStateRect.right() &&
       touchPoint.y >= gateStateRect.top() && touchPoint.y < gateStateRect.bottom())
   {
      postGateOpen();
   }
#else
   // No touch hardware on this board: use button A to open the gate instead.
   if (sketch.arduino.buttonA.wasPressed() && !isOpen)
   {
      postGateOpen();
   }
#endif

   {
      static uint32_t lastFrameMillis = 0;
      static uint32_t fpsWindowStartMillis = 0;
      static uint32_t fpsFrameCount = 0;
      static float fps = 0;

      uint32_t frameMillis = millis();
      fpsFrameCount++;
      if (frameMillis - fpsWindowStartMillis >= 1000)
      {
         fps = fpsFrameCount * 1000.0f / (frameMillis - fpsWindowStartMillis);
         fpsWindowStartMillis = frameMillis;
         fpsFrameCount = 0;
      }

      if (isOpen)
      {
         Serial.print("disp t=");
         Serial.print(frameMillis);
         Serial.print(" rawL=");
         Serial.print(leftAzimuth, 1);
         Serial.print(" left=");
         Serial.print(displayLeftAzimuth, 1);
         Serial.print(" dt=");
         Serial.print(frameMillis - lastFrameMillis);
         Serial.print("ms fps=");
         Serial.println(fps, 1);
      }
      lastFrameMillis = frameMillis;

      if (!isnan(displayLeftAzimuth))
      {
         displayLine(leftLine, displayLeftAzimuth);
      }

      if (!isnan(displayRightAzimuth))
      {
         displayLine(rightLine, displayRightAzimuth);
      }

      if (isOpen)
      {
         Serial.print("drawn t=");
         Serial.print(millis());
         Serial.print(" drawMs=");
         Serial.println(millis() - frameMillis);
      }
   }
}
