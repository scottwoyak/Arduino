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

#include <array>
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
   bool fullPush = true;
   int16_t dirtyLeft = 0;
   int16_t dirtyTop = 0;
   int16_t dirtyRight = 0;
   int16_t dirtyBottom = 0;
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
// This sketch only uses monospaced text, so leave out the proportional Roboto fonts.
#define NO_PROPORTIONAL_FONT
#define TEXT_SIZES_CUSTOM
#define TEXT_SIZE_2
#define TEXT_SIZE_3
#ifndef ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT
// Sizes 4 and 5 are only used by the touch-only history/settings views and the larger
// gate state banner, so the non-touch Feather doesn't need them.
#define TEXT_SIZE_4
#define TEXT_SIZE_5
#endif

// Only compile in the notification sounds selectable in the settings view, to save flash.
#define SOUNDS_CUSTOM
#define SOUND_AMOK_TIME
#define SOUND_BOND
#define SOUND_CAFE_BELL
#define SOUND_CATHEDRAL
#define SOUND_CHICKENS
#define SOUND_DANGER
#define SOUND_GONG_MUSIC
#define SOUND_LOTR_BATTLE
#define SOUND_NUCLEAR
#define SOUND_OBLITERATE
#define SOUND_RAVEN
#define SOUND_RED_ALERT
#define SOUND_MORNING
#define SOUND_SEWS
#define SOUND_SIREN
#define SOUND_UNDERTAKER
#define SOUND_WHISTLE

#include "ArduinoBoard.h"
#include "LibraryVersion.h"

// This sketch's own version (e.g. "1.06"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("1.10");
constexpr auto SKETCH_NAME = "Gate_Viewer";

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Viewer)."
#include "WrongBoard.h"
#endif

// The settings view (gear icon, volume slider and sound list) needs both touch and sound.
#if defined(ARDUINO_TOUCH_SUPPORTED) && defined(ARDUINO_SOUND_SUPPORTED)
#define SETTINGS_SUPPORTED
#endif

#include <HTTPClient.h>
#include <WebSocketsClient.h>

#include "DirtySprite.h"
#include "SerialX.h"
#include "Slider.h"
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

// Antialiased drawing: the line's half-width, the extra half-width used when erasing it (to
// cover its soft edge pixels), and the thickness of the origin circle's ring.
constexpr float GATE_LINE_RADIUS = 1.5f;
constexpr float GATE_LINE_ERASE_PAD = 1.5f;
constexpr int16_t GATE_CIRCLE_THICKNESS = 2;

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
/// Gets how many calendar days ago a time was, relative to the current time.
/// </summary>
/// <param name="time">Time to compare.</param>
/// <returns>0 for today, 1 for yesterday, etc.; -1 if the clock isn't synced or the time is in the future.</returns>
///
int daysAgo(time_t time)
{
   if (!TimeSync::isSynced())
   {
      return -1;
   }

   time_t nowTime = ::time(nullptr);
   struct tm nowInfo;
   struct tm thenInfo;
   localtime_r(&nowTime, &nowInfo);
   localtime_r(&time, &thenInfo);

   // Normalize both to local midnight; tm_isdst = -1 lets mktime resolve DST.
   nowInfo.tm_hour = nowInfo.tm_min = nowInfo.tm_sec = 0;
   thenInfo.tm_hour = thenInfo.tm_min = thenInfo.tm_sec = 0;
   nowInfo.tm_isdst = -1;
   thenInfo.tm_isdst = -1;
   double days = difftime(mktime(&nowInfo), mktime(&thenInfo)) / 86400.0;
   if (days < -0.5)
   {
      return -1;
   }

   return (int)lround(days);
}

///
/// <summary>
/// Formats a time_t as a friendly date string: "Today" or "Yesterday" for recent dates,
/// otherwise e.g. "Aug 12th", using a 3-letter month abbreviation and an ordinal day
/// suffix (st/nd/rd/th).
/// </summary>
/// <param name="time">Time to format.</param>
/// <returns>Friendly date string, e.g. "Today", "Yesterday" or "Aug 12th".</returns>
///
std::string formatFriendlyDate(time_t time)
{
   int days = daysAgo(time);
   if (days == 0)
   {
      return "Today";
   }
   if (days == 1)
   {
      return "Yesterday";
   }

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
/// Formats a time_t as a compact date string, "Today" or "Yesterday" for recent dates,
/// otherwise numeric, e.g. "9/22", for narrower
/// displays that don't have room for formatFriendlyDate()'s longer form.
/// </summary>
/// <param name="time">Time to format.</param>
/// <returns>Compact date string, e.g. "Today" or "9/22".</returns>
///
std::string formatShortDate(time_t time)
{
   int days = daysAgo(time);
   if (days == 0)
   {
      return "Today";
   }
   if (days == 1)
   {
      return "Yesterday";
   }

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
   Color messageColor = Color::GRAY;

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

#ifdef ARDUINO_TOUCH_SUPPORTED
// ----------- Gear/close icon geometry (shared by the main, history and settings views)
constexpr int16_t GEAR_RADIUS = 16;
constexpr int16_t GEAR_MARGIN = 6;
constexpr int16_t GEAR_TAP_SIZE = 56;

///
/// <summary>
/// Draws the close icon (an X) in the upper right corner of the history and settings views.
/// </summary>
///
void drawCloseIcon()
{
   const int16_t cx = sketch.arduino.width() - GEAR_MARGIN - GEAR_RADIUS;
   const int16_t cy = GEAR_MARGIN + GEAR_RADIUS;
   const int16_t d = GEAR_RADIUS * 7 / 10;

   for (int16_t offset = -1; offset <= 1; offset++)
   {
      sketch.arduino.display.drawLine(cx - d + offset, cy - d, cx + d + offset, cy + d, (uint16_t)Color::GRAY);
      sketch.arduino.display.drawLine(cx - d + offset, cy + d, cx + d + offset, cy - d, (uint16_t)Color::GRAY);
   }
}

///
/// <summary>
/// Determines whether a touch is on the gear icon (or, in the history and settings views, its close icon).
/// </summary>
/// <param name="x">Touch x coordinate.</param>
/// <param name="y">Touch y coordinate.</param>
/// <returns>True if the touch is within the gear's tap target.</returns>
///
bool onGear(int16_t x, int16_t y)
{
   return x >= sketch.arduino.width() - GEAR_TAP_SIZE && y < GEAR_TAP_SIZE;
}
#endif

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

#ifdef ARDUINO_TOUCH_SUPPORTED
   drawCloseIcon();
#endif

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

#ifdef SETTINGS_SUPPORTED
// ----------- Settings view (shown when the gear icon in the upper right is tapped)
constexpr auto VOLUME_PREFERENCES_KEY = "volume";
constexpr auto SOUND_PREFERENCES_KEY = "sound";
constexpr float DEFAULT_VOLUME = 2.0f;
constexpr float MAX_VOLUME = 2.5f;

// The slider shows volume as 0-1; the sound's actual volume is this times MAX_VOLUME.
float settingsVolume = DEFAULT_VOLUME / MAX_VOLUME;
constexpr uint8_t DEFAULT_SOUND_INDEX = 5;

// Sound indices (see Sound::soundIndex) selectable in the settings view, in display order.
constexpr std::array<uint8_t, 17> SETTINGS_SOUNDS = {
   12, // Amok Time
   10, // Bond
   25, // Cafe Bell
   15, // Cathedral
   5,  // Chickens
   9,  // Danger
   18, // Gong Music
   21, // LOTR Battle
   7,  // Nuclear
   8,  // Obliterate
   6,  // Raven
   13, // Red Alert
   2,  // Rooster
   20, // SEWS
   19, // Siren
   17, // Undertaker
   11, // Whistle
};
constexpr uint8_t NUM_SETTINGS_SOUNDS = SETTINGS_SOUNDS.size();

constexpr uint8_t SETTINGS_TEXT_SIZE = 2;
constexpr int16_t SETTINGS_SLIDER_Y = 64;
constexpr int16_t SETTINGS_SLIDER_HEIGHT = 56;
constexpr int16_t SETTINGS_LIST_TOP = SETTINGS_SLIDER_Y + SETTINGS_SLIDER_HEIGHT + 10;
constexpr int16_t SETTINGS_ROW_HEIGHT = 30;
constexpr int16_t SETTINGS_ROW_GAP = 4;
constexpr int16_t SETTINGS_ARROW_HEIGHT = 36;
constexpr int16_t SETTINGS_ARROW_SIZE = 10;

Slider settingsVolumeSlider(
   &sketch.arduino,
   "Volume",
   2,
   "",
   0.0f,
   1.0f,
   &settingsVolume,
   0,
   SETTINGS_SLIDER_Y,
   0,
   SETTINGS_SLIDER_HEIGHT);

LGFX_Sprite settingsListSprite(&sketch.arduino.display);
bool settingsListSpriteCreated = false;
uint8_t settingsListFirst = 0;

///
/// <summary>
/// Loads the saved volume and sound selection from Preferences and applies them. Falls
/// back to the defaults if nothing is saved or the saved sound isn't selectable.
/// </summary>
///
void loadSettings()
{
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
   float volume = sketch.arduino.preferences.getFloat(VOLUME_PREFERENCES_KEY, DEFAULT_VOLUME);
   uint8_t soundIndex = sketch.arduino.preferences.getUChar(SOUND_PREFERENCES_KEY, DEFAULT_SOUND_INDEX);
   sketch.arduino.preferences.end();

   bool soundValid = false;
   for (uint8_t index : SETTINGS_SOUNDS)
   {
      soundValid = soundValid || index == soundIndex;
   }

   sketch.arduino.sound.volume = constrain(volume, 0.0f, MAX_VOLUME);
   settingsVolume = sketch.arduino.sound.volume / MAX_VOLUME;
   sketch.arduino.sound.soundIndex = soundValid ? soundIndex : DEFAULT_SOUND_INDEX;
}

///
/// <summary>
/// Saves the current volume and sound selection to Preferences.
/// </summary>
///
void saveSettings()
{
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putFloat(VOLUME_PREFERENCES_KEY, sketch.arduino.sound.volume);
   sketch.arduino.preferences.putUChar(SOUND_PREFERENCES_KEY, sketch.arduino.sound.soundIndex);
   sketch.arduino.preferences.end();
}

///
/// <summary>
/// Draws the gear icon in the upper right corner of the display.
/// </summary>
///
void drawGear()
{
   const int16_t cx = sketch.arduino.width() - GEAR_MARGIN - GEAR_RADIUS;
   const int16_t cy = GEAR_MARGIN + GEAR_RADIUS;
   const uint16_t color = (uint16_t)Color::GRAY;

   constexpr uint8_t NUM_TEETH = 8;
   for (uint8_t i = 0; i < NUM_TEETH; i++)
   {
      const float angle = i * 2.0f * (float)M_PI / NUM_TEETH;
      sketch.arduino.display.fillCircle(
         cx + (int16_t)lround(GEAR_RADIUS * 0.8f * cos(angle)),
         cy + (int16_t)lround(GEAR_RADIUS * 0.8f * sin(angle)),
         GEAR_RADIUS / 5,
         color);
   }

   sketch.arduino.display.fillCircle(cx, cy, GEAR_RADIUS * 7 / 10, color);
   sketch.arduino.display.fillCircle(cx, cy, GEAR_RADIUS * 3 / 10, (uint16_t)Color::BLACK);
}

bool gearVisible = true;

///
/// <summary>
/// Shows or hides the gear icon (hidden by painting its area black), remembering the state.
/// </summary>
/// <param name="visible">True to draw the gear; false to erase it.</param>
///
void updateGear(bool visible)
{
   gearVisible = visible;
   if (visible)
   {
      drawGear();
   }
   else
   {
      sketch.arduino.display.fillRect(
         sketch.arduino.width() - GEAR_MARGIN - 2 * GEAR_RADIUS - 1,
         GEAR_MARGIN - 1,
         2 * GEAR_RADIUS + 2,
         2 * GEAR_RADIUS + 2,
         (uint16_t)Color::BLACK);
   }
}

///
/// <summary>
/// Gets the number of sound rows that fit in the list above the scroll arrows.
/// </summary>
/// <returns>Number of visible rows.</returns>
///
uint8_t settingsVisibleRows()
{
   return (sketch.arduino.height() - SETTINGS_LIST_TOP - SETTINGS_ARROW_HEIGHT) / SETTINGS_ROW_HEIGHT;
}

///
/// <summary>
/// Gets the y coordinate (relative to the top of the list) where the scroll arrows start.
/// </summary>
/// <returns>Arrow top in pixels.</returns>
///
int16_t settingsArrowY()
{
   return settingsVisibleRows() * SETTINGS_ROW_HEIGHT;
}

///
/// <summary>
/// Draws the scrollable sound list and its up/down scroll buttons, highlighting the
/// selected sound.
/// </summary>
///
void drawSettingsList()
{
   const int16_t width = sketch.arduino.width();
   const int16_t listHeight = sketch.arduino.height() - SETTINGS_LIST_TOP;
   const uint8_t visibleRows = settingsVisibleRows();
   const int16_t arrowY = settingsArrowY();

   if (!settingsListSpriteCreated)
   {
      // The list sprite is large (~220 KB), too big for internal RAM alongside WiFi/TLS
      settingsListSprite.setPsram(true);
      sketch.arduino.createSprite(settingsListSprite, width, listHeight, SETTINGS_TEXT_SIZE);
      settingsListSpriteCreated = true;
   }

   settingsListSprite.fillScreen((uint16_t)Color::BLACK);
   for (uint8_t row = 0; row < visibleRows; row++)
   {
      const uint8_t i = settingsListFirst + row;
      if (i >= NUM_SETTINGS_SOUNDS)
      {
         break;
      }

      const int16_t y = row * SETTINGS_ROW_HEIGHT;
      const uint16_t fillColor = (uint16_t)(SETTINGS_SOUNDS[i] == sketch.arduino.sound.soundIndex ? Color::BLUE : Color::DARKGRAY);
      const char* name = Sound::soundName(SETTINGS_SOUNDS[i]);
      settingsListSprite.fillRect(0, y, width, SETTINGS_ROW_HEIGHT - SETTINGS_ROW_GAP, fillColor);
      settingsListSprite.setTextColor((uint16_t)Color::WHITE, fillColor);
      settingsListSprite.setCursor(
         (width - settingsListSprite.textWidth(name)) / 2,
         y + (SETTINGS_ROW_HEIGHT - SETTINGS_ROW_GAP - sketch.arduino.charH(SETTINGS_TEXT_SIZE)) / 2);
      settingsListSprite.print(name);
   }

   const int16_t halfWidth = width / 2;
   const int16_t arrowH = listHeight - arrowY;
   const bool canUp = settingsListFirst > 0;
   const bool canDown = settingsListFirst + visibleRows < NUM_SETTINGS_SOUNDS;
   const uint16_t upColor = (uint16_t)(canUp ? Color::WHITE : Color::DARKGRAY);
   const uint16_t downColor = (uint16_t)(canDown ? Color::WHITE : Color::DARKGRAY);

   settingsListSprite.fillRect(0, arrowY, halfWidth - SETTINGS_ROW_GAP / 2, arrowH, (uint16_t)Color::DIMGRAY);
   settingsListSprite.fillRect(halfWidth + SETTINGS_ROW_GAP / 2, arrowY, halfWidth - SETTINGS_ROW_GAP / 2, arrowH, (uint16_t)Color::DIMGRAY);

   const int16_t upCx = halfWidth / 2;
   const int16_t downCx = halfWidth + halfWidth / 2;
   const int16_t cy = arrowY + arrowH / 2;
   constexpr int16_t T = SETTINGS_ARROW_SIZE;
   settingsListSprite.fillTriangle(upCx, cy - T, upCx - T, cy + T, upCx + T, cy + T, upColor);
   settingsListSprite.fillTriangle(downCx, cy + T, downCx - T, cy - T, downCx + T, cy - T, downColor);
   settingsListSprite.pushSprite(0, SETTINGS_LIST_TOP);
}

///
/// <summary>
/// Draws the full-screen settings view: title, close icon, volume slider and sound list.
/// </summary>
///
void displaySettingsView()
{
   sketch.arduino.clearDisplay();

   sketch.arduino.setTextSize(HISTORY_TITLE_TEXT_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Settings", Color::HEADING);

   drawCloseIcon();

   settingsVolumeSlider.setWidth(sketch.arduino.width());
   settingsVolumeSlider.draw();
   drawSettingsList();
}

///
/// <summary>
/// Handles touch input while the settings view is shown: dragging the volume slider,
/// selecting and scrolling the sound list. Changes are saved and the selected sound is
/// played as feedback.
/// </summary>
/// <param name="touched">True if the display is currently touched.</param>
/// <param name="tapped">True if a new touch just started.</param>
/// <param name="x">Touch x coordinate.</param>
/// <param name="y">Touch y coordinate.</param>
/// <returns>True if the user closed the settings view.</returns>
///
bool updateSettingsView(bool touched, bool tapped, int16_t x, int16_t y)
{
   if (tapped && onGear(x, y))
   {
      return true;
   }

   const bool volumeReleased = settingsVolumeSlider.update(touched, x, y);
   sketch.arduino.sound.volume = settingsVolume * MAX_VOLUME;
   if (volumeReleased)
   {
      saveSettings();
      sketch.arduino.sound.playNotificationAsync();
   }

   if (tapped && y >= SETTINGS_LIST_TOP)
   {
      const int16_t listY = y - SETTINGS_LIST_TOP;
      const uint8_t visibleRows = settingsVisibleRows();
      if (listY >= settingsArrowY())
      {
         const bool up = x < sketch.arduino.width() / 2;
         if (up && settingsListFirst > 0)
         {
            settingsListFirst--;
            drawSettingsList();
         }
         else if (!up && settingsListFirst + visibleRows < NUM_SETTINGS_SOUNDS)
         {
            settingsListFirst++;
            drawSettingsList();
         }
      }
      else
      {
         const uint8_t index = settingsListFirst + listY / SETTINGS_ROW_HEIGHT;
         if (index < NUM_SETTINGS_SOUNDS)
         {
            sketch.arduino.sound.soundIndex = SETTINGS_SOUNDS[index];
            saveSettings();
            drawSettingsList();
            sketch.arduino.sound.playNotificationAsync();
         }
      }
   }

   return false;
}
#endif

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

// ----------- Hold open button, shown below the "OPEN" banner on touch boards while the
// gate is open. Tapping it holds the gate open (button then reads "Release") until tapped again.
Rect16 holdButtonRect;
bool gateHeldOpen = false;

///
/// <summary>
/// Posts a command to the Gate_Opener's /Gate endpoint.
/// </summary>
/// <param name="command">"OPEN" to pulse the gate open, "HOLD" to hold it open, or "CLOSE" to release a hold.</param>
///
void postGateCommand(const char* command)
{
   HTTPClient http;
   String url = String("http://") + GATE_OPENER_HOST + ":" + GATE_OPENER_PORT + "/Gate";
   http.begin(url);
   http.addHeader("Content-Type", "text/plain");
   int code = http.POST(command);
   http.end();

   if (code == HTTP_CODE_OK)
   {
      Logger.log(std::string("Gate ") + command + " request sent", LogSeverity::INFO, "Gate");
   }
   else
   {
      Logger.log(
         std::string("Gate ") + command + " request failed: " + HTTPClient::errorToString(code).c_str() + " (" + std::to_string(code) + ")",
         LogSeverity::ERROR,
         "Gate");
   }
}

///
/// <summary>
/// Posts "OPEN" to the Gate_Opener's /Gate endpoint to trigger the gate to open.
/// </summary>
///
void postGateOpen()
{
   postGateCommand("OPEN");
}

#ifdef ARDUINO_TOUCH_SUPPORTED
constexpr auto HOLD_SUBTEXT_OPEN = "Tap to hold open";
constexpr auto HOLD_SUBTEXT_CLOSE = "  Tap to close  ";

///
/// <summary>
/// Draws the hint beneath the "OPEN" banner: "Tap to hold open" or, while held, "Tap to
/// close". Both strings are padded to the same width so redrawing one over the other
/// fully overwrites it without clearing the screen, which avoids flicker.
/// </summary>
///
void drawHoldSubtext()
{
   Point16 savedCursor = sketch.arduino.getCursor();
   uint8_t savedTextSize = sketch.arduino.getTextSize();

   sketch.arduino.setTextSize(2);
   sketch.arduino.setCursor(0, GATE_STATE_TOP_MARGIN + sketch.arduino.charH(GATE_STATE_TEXT_SIZE));
   sketch.arduino.printlnC(gateHeldOpen ? HOLD_SUBTEXT_CLOSE : HOLD_SUBTEXT_OPEN, Color::GRAY, Color::BLACK);

   sketch.arduino.setTextSize(savedTextSize);
   sketch.arduino.setCursor(savedCursor);
}
#endif

///
/// <summary>
/// Gets the banner title for the open gate.
/// </summary>
/// <param name="motion">+1 if the gate is opening, -1 if closing, 0 if stationary.</param>
/// <returns>"OPENING", "CLOSING", "HELD OPEN" or "OPEN".</returns>
///
const char* openTitle(int8_t motion)
{
   if (motion > 0)
   {
      return "OPENING";
   }
   if (motion < 0)
   {
      return "CLOSING";
   }
   return gateHeldOpen ? "HELD OPEN" : "OPEN";
}

int8_t gateDisplayedMotion = 0;
extern float leftValue;
extern float rightValue;

///
/// <summary>
/// Immediately shows "OPENING" after an open request has been made, without waiting for the
/// gate telemetry to report movement. Repainted by displayGateState() once the gate is
/// reported open, or reverted by loop() if it never opens.
/// </summary>
///
void showOpeningPending()
{
   Point16 savedCursor = sketch.arduino.getCursor();
   uint8_t savedTextSize = sketch.arduino.getTextSize();

   sketch.arduino.setTextSize(GATE_STATE_TEXT_SIZE);
#ifdef ARDUINO_TOUCH_SUPPORTED
   int16_t titleY = GATE_STATE_TOP_MARGIN;
   sketch.arduino.fillRect(0, 0, sketch.arduino.width(), GATE_STATE_TOP_MARGIN + sketch.arduino.charH(GATE_STATE_TEXT_SIZE) + sketch.arduino.charH(2), Color::BLACK);
#else
   int16_t titleY = (GATE_STATE_TOP_MARGIN + GATE_STATE_BOTTOM_MARGIN) / 2;
   sketch.arduino.fillRect(0, titleY, sketch.arduino.width(), sketch.arduino.charH(GATE_STATE_TEXT_SIZE), Color::BLACK);
#endif
   sketch.arduino.setCursor(0, titleY);
   sketch.arduino.printlnC("OPENING", Color::GRAY, Color::BLACK);

   sketch.arduino.setTextSize(savedTextSize);
   sketch.arduino.setCursor(savedCursor);
}

#ifdef ARDUINO_TOUCH_SUPPORTED
///
/// <summary>
/// While the gate is fully open (open and not moving), periodically asks the Gate_Opener whether it is holding the gate
/// open, so every viewer shows "HELD OPEN" regardless of which one started the hold. Repaints
/// the title and hint only when the held state changes.
/// </summary>
/// <param name="isOpen">True if the gate is open and stationary</param>
///
void pollGateHold(bool isOpen)
{
   constexpr uint32_t HOLD_POLL_SPAN_S = 4;
   constexpr uint16_t HOLD_POLL_TIMEOUT_MS = 500;
   static TimerSecs pollTimer(HOLD_POLL_SPAN_S);

   if (!isOpen || !pollTimer.ready())
   {
      return;
   }

   HTTPClient http;
   String url = String("http://") + GATE_OPENER_HOST + ":" + GATE_OPENER_PORT + "/Gate";
   http.setConnectTimeout(HOLD_POLL_TIMEOUT_MS);
   http.setTimeout(HOLD_POLL_TIMEOUT_MS);
   http.begin(url);
   int code = http.GET();
   String body = (code == 200) ? http.getString() : String();
   http.end();

   static bool pollFailing = false;
   if (code != 200)
   {
      if (!pollFailing)
      {
         pollFailing = true;
         Logger.log(
            std::string("Gate opener hold-state poll failed: ") + HTTPClient::errorToString(code).c_str() + " (" + std::to_string(code) + ")",
            LogSeverity::WARN,
            "Gate");
      }
      return;
   }

   if (pollFailing)
   {
      pollFailing = false;
      Logger.log("Gate opener hold-state poll recovered", LogSeverity::INFO, "Gate");
   }

   body.trim();
   bool held = body.equalsIgnoreCase("HOLD");
   if (held == gateHeldOpen)
   {
      return;
   }

   gateHeldOpen = held;
   Point16 savedCursor = sketch.arduino.getCursor();
   uint8_t savedTextSize = sketch.arduino.getTextSize();
   sketch.arduino.setTextSize(GATE_STATE_TEXT_SIZE);
   sketch.arduino.fillRect(0, GATE_STATE_TOP_MARGIN, sketch.arduino.width(), sketch.arduino.charH(GATE_STATE_TEXT_SIZE), Color::BLACK);
   sketch.arduino.setCursor(0, GATE_STATE_TOP_MARGIN);
   sketch.arduino.printlnC(openTitle(gateDisplayedMotion), Color::GRAY, Color::BLACK);
   drawHoldSubtext();
#ifdef SETTINGS_SUPPORTED
   if (gearVisible)
   {
      drawGear();
   }
#endif
   sketch.arduino.setTextSize(savedTextSize);
   sketch.arduino.setCursor(savedCursor);
}
#endif

///
/// <summary>
/// Draws the overall gate state (CLOSED, or OPEN/OPENING/CLOSING/HELD OPEN) in size 5 text
/// with a "Tap to ..." hint below it on touch-capable boards. Open is shown by inverting
/// the panel. When only the open title changes, just the title row is repainted to avoid
/// clearing the whole screen.
/// </summary>
/// <param name="isOpen">True if either gate's azimuth is above the open threshold.</param>
/// <param name="forceRedraw">If true, redraws even if nothing changed since the last call (e.g. after returning from the history view).</param>
/// <param name="motion">+1 if the gate is opening, -1 if closing, 0 if stationary.</param>
///
void displayGateState(bool isOpen, bool forceRedraw = false, int8_t motion = 0)
{
   static bool lastIsOpen = false;
   static int8_t lastMotion = 0;
   static bool everDrawn = false;

   if (everDrawn && !forceRedraw && isOpen == lastIsOpen && motion == lastMotion)
   {
      return;
   }

   bool titleOnly = everDrawn && !forceRedraw && isOpen == lastIsOpen && isOpen;

   lastIsOpen = isOpen;
   lastMotion = motion;
   gateDisplayedMotion = motion;
   everDrawn = true;

   Point16 savedCursor = sketch.arduino.getCursor();
   uint8_t savedTextSize = sketch.arduino.getTextSize();

   sketch.arduino.setTextSize(GATE_STATE_TEXT_SIZE);

   if (titleOnly)
   {
      // Only the OPEN/OPENING/CLOSING title changed, so repaint just that row.
#ifdef ARDUINO_TOUCH_SUPPORTED
      int16_t titleY = GATE_STATE_TOP_MARGIN;
#else
      int16_t titleY = (GATE_STATE_TOP_MARGIN + GATE_STATE_BOTTOM_MARGIN) / 2;
#endif
      sketch.arduino.fillRect(0, titleY, sketch.arduino.width(), sketch.arduino.charH(GATE_STATE_TEXT_SIZE), Color::BLACK);
      sketch.arduino.setCursor(0, titleY);
                   sketch.arduino.printlnC(openTitle(motion), Color::GRAY, Color::BLACK);

                   sketch.arduino.setTextSize(savedTextSize);
      sketch.arduino.setCursor(savedCursor);
      return;
   }

   Color backgroundColor = Color::BLACK;
#ifndef ARDUINO_TOUCH_SUPPORTED
   int16_t rowHeight = GATE_STATE_TOP_MARGIN + sketch.arduino.charH(GATE_STATE_TEXT_SIZE) + GATE_STATE_BOTTOM_MARGIN;
#endif
   sketch.arduino.fillRect(0, 0, sketch.arduino.width(), sketch.arduino.height(), backgroundColor);

   if (isOpen)
   {
#ifdef ARDUINO_TOUCH_SUPPORTED
      sketch.arduino.setCursor(0, GATE_STATE_TOP_MARGIN);
      sketch.arduino.printlnC(openTitle(motion), Color::GRAY, Color::BLACK);

      drawHoldSubtext();

      // Tappable area spans the full width, from the top down to halfway down the display.
      holdButtonRect = Rect16(0, 0, sketch.arduino.width(), sketch.arduino.height() * 3 / 4);
#else
      sketch.arduino.setCursor(0, (rowHeight - sketch.arduino.charH(GATE_STATE_TEXT_SIZE)) / 2);
      sketch.arduino.printlnC(openTitle(motion), Color::GRAY, Color::BLACK);
#endif

      gateStateRect = { 0, 0, 0, 0 };
   }
   else
   {
      sketch.arduino.setCursor(0, GATE_STATE_TOP_MARGIN);
      sketch.arduino.printlnC("CLOSED", Color::GRAY, Color::BLACK);

#ifdef ARDUINO_TOUCH_SUPPORTED
      // Tappable area spans the full width, from the top down to halfway down the display.
      gateStateRect = Rect16(0, 0, sketch.arduino.width(), sketch.arduino.height() * 3 / 4);

      sketch.arduino.setTextSize(2);
      sketch.arduino.moveCursorY(-4);
      sketch.arduino.printlnC("Tap to open", Color::GRAY, Color::BLACK);
#else
      gateStateRect = { 0, 0, 0, 0 };
#endif

      holdButtonRect = { 0, 0, 0, 0 };
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
bool displayLine(LineState& line, float azimuth)
{
   constexpr float MIN_REDRAW_DEGREES = 0.1f;
   if (!isnan(line.lastAzimuth) && fabsf(azimuth - line.lastAzimuth) <= MIN_REDRAW_DEGREES)
   {
      return false;
   }

   float azimuthRad = azimuth * (float)M_PI / 180.0f;
   float xDir = line.mirrorX ? -cos(azimuthRad) : cos(azimuthRad);
   float yDir = sin(azimuthRad);
   int16_t startX = line.startX + (int16_t)lround(gateOriginRadius * xDir);
   int16_t startY = gateOriginY - (int16_t)lround(gateOriginRadius * yDir);
   int16_t endX = line.startX + (int16_t)lround(lineLength * xDir);
   int16_t endY = gateOriginY - (int16_t)lround(lineLength * yDir);

   // Region that changed: bounds of the old and new line, padded for line width and
   // antialiasing. A full push is used when there's no previous line to compare with
   // (e.g. first draw, or after the background was repainted).
   constexpr int16_t DIRTY_PAD = 4;
   line.fullPush = isnan(line.lastAzimuth) || !line.lineDrawn;
   int16_t minX = min<int16_t>(startX, endX);
   int16_t maxX = max<int16_t>(startX, endX);
   int16_t minY = min<int16_t>(startY, endY);
   int16_t maxY = max<int16_t>(startY, endY);
   if (line.lineDrawn)
   {
      minX = min<int16_t>(minX, min<int16_t>(line.lastStartX, line.lastEndX));
      maxX = max<int16_t>(maxX, max<int16_t>(line.lastStartX, line.lastEndX));
      minY = min<int16_t>(minY, min<int16_t>(line.lastStartY, line.lastEndY));
      maxY = max<int16_t>(maxY, max<int16_t>(line.lastStartY, line.lastEndY));
   }
   line.dirtyLeft = minX - DIRTY_PAD;
   line.dirtyTop = minY - DIRTY_PAD;
   line.dirtyRight = maxX + DIRTY_PAD;
   line.dirtyBottom = maxY + DIRTY_PAD;

   line.lastStartX = startX;
   line.lastStartY = startY;
   line.lastEndX = endX;
   line.lastEndY = endY;
   line.lineDrawn = true;
   line.lastAzimuth = azimuth;
   return true;
}

///
/// <summary>
/// Composes one gate's line and origin circle in its own off-screen sprite covering that
/// gate's half of the display, and pushes it in one operation, avoiding flicker. Each gate
/// only draws within its own half, so only the gate that changed needs to be rendered.
/// </summary>
/// <param name="line">Gate line state to render (mirrorX identifies the right gate).</param>
///
void renderLine(LineState& line)
{
   static DirtySprite sprites[2];

   const int16_t displayWidth = sketch.arduino.width();
   const int16_t halfWidth = displayWidth / 2;
   const int16_t x0 = line.mirrorX ? halfWidth : 0;
   const int16_t width = line.mirrorX ? displayWidth - halfWidth : halfWidth;
   DirtySprite& dirtySprite = sprites[line.mirrorX ? 1 : 0];

   const int16_t top = max<int16_t>(0, gateOriginY - lineLength - 4);
   const int16_t bottom = min<int16_t>(sketch.arduino.height(), gateOriginY + gateOriginRadius + 2);
   dirtySprite.begin(&sketch.arduino.display, x0, top, width, bottom - top);

   lgfx::LGFX_Sprite* sprite = dirtySprite.sprite();
   sprite->fillSprite((uint16_t)Color::BLACK);
   dirtySprite.drawAntialiasedRing(line.startX - x0, gateOriginY - top, gateOriginRadius, GATE_CIRCLE_THICKNESS);

   if (line.lineDrawn)
   {
      sprite->drawWideLine(line.lastStartX - x0, line.lastStartY - top, line.lastEndX - x0, line.lastEndY - top, GATE_LINE_RADIUS, (uint16_t)Color::WHITE);
   }

   if (line.fullPush)
   {
      dirtySprite.push();
   }
   else
   {
      dirtySprite.push(line.dirtyLeft, line.dirtyTop, line.dirtyRight, line.dirtyBottom);
   }
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

constexpr uint32_t HEAP_LOG_INTERVAL_MS = 10 * 60 * 1000;
Timer heapLogTimer(HEAP_LOG_INTERVAL_MS);

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

#ifdef SETTINGS_SUPPORTED
   loadSettings();
#endif

   TelemetrySubscriber* client = sketch.beginTelemetry(LEFT_TELEMETRY_TOPIC, &telemetryHandler);
   client->onSample([](const std::string& topic, double value, int64_t dtMicros)
   {
      if (topic == LEFT_TELEMETRY_TOPIC)
      {
         leftValue = (float)value;
               }
      else if (topic == RIGHT_TELEMETRY_TOPIC)
      {
         rightValue = (float)value;
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

   if (heapLogTimer.ready())
   {
      Logger.log(
         "Heap: free " + std::to_string(ESP.getFreeHeap()) +
         ", largest free block " + std::to_string(ESP.getMaxAllocHeap()) +
         ", min free " + std::to_string(ESP.getMinFreeHeap()),
         LogSeverity::INFO,
         "Heap");
   }

   if (sketch.isLocating())
   {
      return;
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
   bool tapped = false;
#endif

   static bool showingHistory = false;
   static TimerSecs historyTimeoutTimer(HISTORY_VIEW_TIMEOUT_S);

   bool forceRedraw = redrawAfterLocate;
   redrawAfterLocate = false;

   #ifdef SETTINGS_SUPPORTED
   static bool showingSettings = false;
   if (showingSettings)
   {
      if (updateSettingsView(touched, tapped, touchPoint.x, touchPoint.y))
      {
         showingSettings = false;
         tapped = false;
         sketch.arduino.clearDisplay();
         forceRedraw = true;
      }
      else
      {
         return;
      }
   }
   #endif

   if (showingHistory)
   {
      if (tapped || historyTimeoutTimer.ready())
      {
         showingHistory = false;
         tapped = false;
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

   float displayLeftAzimuth = leftAzimuth;
   float displayRightAzimuth = rightAzimuth;

   // The gate is considered open whenever either displayed angle exceeds the threshold.
   // After a tap the gate is expected to move, so any reported movement counts as open.
   // The gate mechanism itself introduces about a half second delay (measured at best
   // ~630 ms from relay on to the first reported movement) before the gate starts moving, so
   // the viewer can't show movement sooner. Because of that, after a tap the panel is shown
   // as opening right away (openPending) rather than waiting for the angle to change.
   static bool openPending = false;
   static TimerSecs openPendingTimer(10.0f);

   // A resting gate jitters around a degree or so, so the gate is not declared open by an angle.
   // It opens only after a sustained rise (GATE_OPEN_RISE_DEGREES within GATE_OPEN_RISE_WINDOW_MS)
   // above the recent low, and it closes once the angle falls below the larger
   // GATE_CLOSE_DEGREES. After a tap, any reported angle above 0 counts as open.
   constexpr float GATE_CLOSE_DEGREES = 3.0f;
   constexpr float GATE_OPEN_RISE_DEGREES = 2.0f;
   constexpr uint32_t GATE_OPEN_RISE_WINDOW_MS = 1000;
   static bool lastIsOpen = false;
   static bool reachedCloseAngle = false;
   static uint32_t openedMs = 0;
   constexpr uint32_t GATE_CLOSE_GRACE_MS = 10000;
   static float riseBaseline = NAN;
   static uint32_t riseBaselineMs = 0;

   float peakAzimuth = fmaxf(isnan(leftAzimuth) ? 0.0f : leftAzimuth, isnan(rightAzimuth) ? 0.0f : rightAzimuth);

   // With no data (e.g. telemetry dropped), keep the previous state rather than treating it
   // as closed, so a reconnect doesn't look like the gate opening again.
   bool isOpen = lastIsOpen;
   if (!isnan(leftAzimuth) || !isnan(rightAzimuth))
   {
      if (lastIsOpen)
      {
            riseBaseline = NAN;
            // A gate that just opened starts below the close angle, so it can't close until it
            // has first passed that angle (or the grace period expires).
            if (peakAzimuth >= GATE_CLOSE_DEGREES)
            {
               reachedCloseAngle = true;
            }
            isOpen = peakAzimuth >= GATE_CLOSE_DEGREES || (!reachedCloseAngle && millis() - openedMs < GATE_CLOSE_GRACE_MS);
         }
      else if (openPending)
      {
         isOpen = peakAzimuth > 0.0f;
      }
      else
      {
         uint32_t nowMs = millis();
         if (isnan(riseBaseline) || peakAzimuth < riseBaseline || nowMs - riseBaselineMs > GATE_OPEN_RISE_WINDOW_MS)
         {
            riseBaseline = peakAzimuth;
            riseBaselineMs = nowMs;
         }
               isOpen = peakAzimuth - riseBaseline >= GATE_OPEN_RISE_DEGREES;
            }

            if (isOpen && !lastIsOpen)
            {
               reachedCloseAngle = false;
               openedMs = millis();
            }
         }

   if (!isOpen && !openPending)
   {
      if (!isnan(displayLeftAzimuth))
      {
         displayLeftAzimuth = 0.0f;
      }
      if (!isnan(displayRightAzimuth))
      {
         displayRightAzimuth = 0.0f;
      }
   }

   if (!isOpen)
   {
      gateHeldOpen = false;
   }

   // Direction of gate travel: +1 opening, -1 closing, 0 stationary (no change for 1 second).
   constexpr float MOTION_MIN_DEGREES = 0.5f;
   constexpr uint32_t MOTION_IDLE_MS = 1000;
   static float lastMotionAzimuth = NAN;
   static uint32_t lastMotionMs = 0;
   static int8_t motion = 0;
   // Motion uses the raw telemetry, which the publisher now reports continuously.
   float motionAzimuth = fmaxf(isnan(leftAzimuth) ? 0.0f : leftAzimuth, isnan(rightAzimuth) ? 0.0f : rightAzimuth);
   if (isnan(lastMotionAzimuth))
   {
      lastMotionAzimuth = motionAzimuth;
   }
   else if (fabsf(motionAzimuth - lastMotionAzimuth) > MOTION_MIN_DEGREES)
   {
      motion = motionAzimuth > lastMotionAzimuth ? 1 : -1;
      lastMotionAzimuth = motionAzimuth;
      lastMotionMs = millis();
   }
   else if (motion != 0 && millis() - lastMotionMs > MOTION_IDLE_MS)
   {
      motion = 0;
   }
   constexpr float FULLY_OPEN_DEGREES = 80.0f;
   if (motionAzimuth >= FULLY_OPEN_DEGREES || (!isOpen && !openPending && motionAzimuth < GATE_CLOSE_DEGREES && !lastIsOpen))
   {
      motion = 0;
   }

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

   displayGateState(isOpen, forceRedraw, isOpen ? motion : 0);
   displayFooterAzimuths(leftAzimuth, rightAzimuth);

       if (stateChanged)
       {
                 sketch.arduino.display.invertDisplay(isOpen);
              }

          // After an open request the panel is inverted right away, in anticipation of the gate
          // moving. If the gate still isn't reported open after the timeout, revert.
          if (openPending && (isOpen || openPendingTimer.ready()))
          {
             openPending = false;
             if (!isOpen)
             {
                sketch.arduino.display.invertDisplay(false);
                displayGateState(false, true);
                leftLine.lastAzimuth = NAN;
                                 rightLine.lastAzimuth = NAN;
                              }
                           }

                #ifdef SETTINGS_SUPPORTED
                           // The gear is only shown while the gate is closed and no open request is pending.
                           // A full redraw (stateChanged) erases it, so redraw it whenever it should be visible.
                           bool wantGear = !isOpen && motion == 0 && !openPending;
                           if (wantGear != gearVisible || (stateChanged && wantGear))
                           {
                              updateGear(wantGear);
                           }
                #endif

       #ifdef ARDUINO_TOUCH_SUPPORTED
   #ifdef SETTINGS_SUPPORTED
       if (tapped && motion == 0 && !openPending && onGear(touchPoint.x, touchPoint.y))
       {
          showingSettings = true;
          sketch.arduino.display.invertDisplay(false);
          settingsListFirst = 0;
          displaySettingsView();
          return;
       }
   #endif

       if (tapped && motion == 0 && !openPending && lastOpenFooterVisible &&
       touchPoint.x >= lastOpenFooterRect.left() && touchPoint.x < lastOpenFooterRect.right() &&
       touchPoint.y >= lastOpenFooterRect.top() && touchPoint.y < lastOpenFooterRect.bottom())
   {
      showingHistory = true;
      sketch.arduino.display.invertDisplay(false);
      historyTimeoutTimer.reset();
      displayHistoryView();
      return;
   }

   if (tapped && isOpen && holdButtonRect.right() > holdButtonRect.left() &&
       touchPoint.x >= holdButtonRect.left() && touchPoint.x < holdButtonRect.right() &&
       touchPoint.y >= holdButtonRect.top() && touchPoint.y < holdButtonRect.bottom())
   {
      gateHeldOpen = !gateHeldOpen;
      postGateCommand(gateHeldOpen ? "HOLD" : "CLOSE");
      sketch.arduino.setTextSize(GATE_STATE_TEXT_SIZE);
      sketch.arduino.fillRect(0, GATE_STATE_TOP_MARGIN, sketch.arduino.width(), sketch.arduino.charH(GATE_STATE_TEXT_SIZE), Color::BLACK);
      sketch.arduino.setCursor(0, GATE_STATE_TOP_MARGIN);
             sketch.arduino.printlnC(openTitle(gateDisplayedMotion), Color::GRAY, Color::BLACK);
                    drawHoldSubtext();
             #ifdef SETTINGS_SUPPORTED
                    if (gearVisible)
                    {
                       drawGear();
                    }
             #endif
                 }

      pollGateHold(isOpen && motion == 0);

      if (tapped && !isOpen &&
       touchPoint.x >= gateStateRect.left() && touchPoint.x < gateStateRect.right() &&
           touchPoint.y >= gateStateRect.top() && touchPoint.y < gateStateRect.bottom())
               {
                  sketch.arduino.display.invertDisplay(true);
                  openPending = true;
                  openPendingTimer.reset();
                  showOpeningPending();
                  postGateOpen();
           }
       #else
           // No touch hardware on this board: use button A to open the gate instead.
           if (sketch.arduino.buttonA.wasPressed() && !isOpen)
           {
              sketch.arduino.display.invertDisplay(true);
              openPending = true;
              openPendingTimer.reset();
              showOpeningPending();
              postGateOpen();
           }
#endif

   if (!isnan(displayLeftAzimuth) && displayLine(leftLine, displayLeftAzimuth))
   {
      renderLine(leftLine);
   }

   if (!isnan(displayRightAzimuth) && displayLine(rightLine, displayRightAzimuth))
   {
         renderLine(rightLine);
      }
}
