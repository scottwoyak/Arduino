//
// SafeMode
//
// Test sketch for exercising the startup watchdog and Safe Mode recovery flow on a
// touchscreen display board.
//
// Behavior:
// - Runs the standard DeviceSketch initialization sequence on boot (Sketch, Version,
//   WiFi, Time, Startup check, and Hub connection).
// - The Startup check step checks whether the "Hang on next restart" flag was set on the
//   previous run. If not set, it clears any saved flag and returns immediately ("OK").
//   If set, it clears the saved flag first (so a restart from Safe Mode returns to the
//   standard display) and then hangs in an infinite loop until the setup task watchdog
//   trips and resets the device into Safe Mode.
// - Once initialization completes, displays a touchscreen UI with a "Hang on next
//   restart" checkbox and a "Restart" button.
//

#include "ArduinoBoard.h"

#if !defined(ARDUINO_DISPLAY_SUPPORTED) || !defined(ARDUINO_TOUCH_SUPPORTED)
#error "This sketch requires a board with a touchscreen display (e.g. Viewer or Waveshare ESP32-S3 Touch LCD)."
#include "WrongBoard.h"
#endif

#include "DeviceSketch.h"
#include "LibraryVersion.h"
#include "SerialX.h"
#include "WiFiSettings.h"

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "SafeMode";
constexpr auto PREFERENCES_NAMESPACE = "SafeMode";
constexpr auto HANG_PREF_KEY = "hang";

constexpr auto TITLE = "Safe Mode Test";
constexpr auto CHECKBOX_LABEL = "Hang on next restart";
constexpr auto RESTART_LABEL = "Restart";
constexpr auto SUBHEADER_HANG = "Expected hang at startup check";
constexpr auto SUBHEADER_NORMAL = "Should start normally";

constexpr uint8_t TITLE_TEXT_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t UI_TEXT_SIZE = DEFAULT_TEXT_SIZE;

constexpr int16_t SCREEN_MARGIN = 16;
constexpr int16_t CHECKBOX_SIZE = 28;
constexpr int16_t CHECKBOX_BORDER = 3;
constexpr int16_t CHECKBOX_LABEL_GAP = 12;
constexpr int16_t CHECKBOX_TOUCH_PAD = 8;
constexpr int16_t CONTROL_ROW_GAP = 28;
constexpr int16_t MAX_BUTTON_WIDTH = 220;
constexpr int16_t MAX_BUTTON_HEIGHT = 72;

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .enableOTA = true,
};

DeviceSketch sketch(SKETCH_CONFIG);

bool shouldHangOnThisBoot = false;
bool hangOnNextRestart = false;
bool checkboxPressed = false;
bool restartPressed = false;

int16_t checkboxX = 0;
int16_t checkboxY = 0;
int16_t checkboxTouchWidth = 0;
Rect16 restartButtonRect;

///
/// <summary>
/// Persists whether the next restart should hang during initialization.
/// </summary>
/// <param name="hang">True to hang during the next initialization; false for a normal startup.</param>
///
void saveHangFlag(bool hang)
{
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putBool(HANG_PREF_KEY, hang);
   sketch.arduino.preferences.end();
}

///
/// <summary>
/// Reads the persisted hang-on-restart flag without modifying it.
/// </summary>
/// <returns>True if the next normal startup should hang during the startup check step.</returns>
///
bool readHangFlag()
{
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
   bool shouldHang = sketch.arduino.preferences.getBool(HANG_PREF_KEY, false);
   sketch.arduino.preferences.end();
   return shouldHang;
}

///
/// <summary>
/// Startup check step that either returns immediately or clears the persisted hang flag
/// (so restarting out of Safe Mode returns to the standard display) and hangs in an
/// infinite loop until the setup watchdog resets the board into Safe Mode.
/// </summary>
/// <returns>True when not hanging.</returns>
///
bool initHangTestStep()
{
   if (shouldHangOnThisBoot)
   {
      saveHangFlag(false);

      for (;;)
      {
         delay(100);
      }
   }

   return true;
}

///
/// <summary>
/// Draws the "Hang on next restart" checkbox and its label.
/// </summary>
///
void drawCheckbox()
{
   sketch.arduino.display.fillRect(
      checkboxX,
      checkboxY,
      CHECKBOX_SIZE,
      CHECKBOX_SIZE,
      (uint16_t)Color::WHITE);

   sketch.arduino.display.fillRect(
      checkboxX + CHECKBOX_BORDER,
      checkboxY + CHECKBOX_BORDER,
      CHECKBOX_SIZE - 2 * CHECKBOX_BORDER,
      CHECKBOX_SIZE - 2 * CHECKBOX_BORDER,
      (uint16_t)(hangOnNextRestart ? Color::BLUE : Color::BLACK));

   sketch.arduino.setTextSize(UI_TEXT_SIZE);
   sketch.arduino.display.setTextColor((uint16_t)Color::WHITE, (uint16_t)Color::BLACK);
   int16_t textY = checkboxY + (CHECKBOX_SIZE - sketch.arduino.charH(UI_TEXT_SIZE)) / 2;
   sketch.arduino.display.setCursor(checkboxX + CHECKBOX_SIZE + CHECKBOX_LABEL_GAP, textY);
   sketch.arduino.display.print(CHECKBOX_LABEL);
}

///
/// <summary>
/// Draws the "Restart" touch button in its normal or pressed state.
/// </summary>
/// <param name="pressed">True to draw with the pressed highlight color; false for the normal color.</param>
///
void drawRestartButton(bool pressed)
{
   Color fillColor = pressed ? Color::LIGHTBLUE : Color::BLUE;

   sketch.arduino.display.fillRect(
      restartButtonRect.x,
      restartButtonRect.y,
      restartButtonRect.width,
      restartButtonRect.height,
      (uint16_t)fillColor);

   sketch.arduino.setTextSize(UI_TEXT_SIZE);
   sketch.arduino.display.setTextColor((uint16_t)Color::WHITE, (uint16_t)fillColor);

   int16_t textX = restartButtonRect.x + (restartButtonRect.width - sketch.arduino.display.textWidth(RESTART_LABEL)) / 2;
   int16_t textY = restartButtonRect.y + (restartButtonRect.height - sketch.arduino.charH(UI_TEXT_SIZE)) / 2;
   sketch.arduino.display.setCursor(textX, textY);
   sketch.arduino.display.print(RESTART_LABEL);
}

///
/// <summary>
/// Computes control positions from the display dimensions and draws the main screen.
/// </summary>
///
void drawMainDisplay()
{
   sketch.arduino.clearDisplay();
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.setTextSize(TITLE_TEXT_SIZE);
   sketch.arduino.println(TITLE, Color::HEADING);

   int16_t headerHeight = sketch.arduino.charH(TITLE_TEXT_SIZE) + SCREEN_MARGIN;
   int16_t availWidth = sketch.arduino.width() - 2 * SCREEN_MARGIN;
   int16_t availHeight = sketch.arduino.height() - headerHeight - SCREEN_MARGIN;

   sketch.arduino.setTextSize(UI_TEXT_SIZE);
   int16_t labelWidth = sketch.arduino.display.textWidth(CHECKBOX_LABEL);
   checkboxTouchWidth = CHECKBOX_SIZE + CHECKBOX_LABEL_GAP + labelWidth;

   int16_t buttonWidth = min<int16_t>(MAX_BUTTON_WIDTH, availWidth);
   int16_t buttonHeight = min<int16_t>(MAX_BUTTON_HEIGHT, max<int16_t>(sketch.arduino.charH(UI_TEXT_SIZE) + 24, availHeight / 3));

   int16_t totalHeight = CHECKBOX_SIZE + CONTROL_ROW_GAP + buttonHeight;
   int16_t top = headerHeight + max<int16_t>(0, (availHeight - totalHeight) / 2);

   checkboxX = max<int16_t>(SCREEN_MARGIN, (sketch.arduino.width() - checkboxTouchWidth) / 2);
   checkboxY = top;

   uint16_t buttonX = (uint16_t)((sketch.arduino.width() - buttonWidth) / 2);
   uint16_t buttonY = (uint16_t)(checkboxY + CHECKBOX_SIZE + CONTROL_ROW_GAP);
   restartButtonRect = Rect16(buttonX, buttonY, (uint16_t)buttonWidth, (uint16_t)buttonHeight);

   drawCheckbox();
   drawRestartButton(false);
}

///
/// <summary>
/// Returns true if the given touch point falls inside the checkbox or its label.
/// </summary>
/// <param name="x">Touch X coordinate.</param>
/// <param name="y">Touch Y coordinate.</param>
/// <returns>True if the point hits the checkbox row.</returns>
///
bool inCheckbox(int16_t x, int16_t y)
{
   return x >= checkboxX - CHECKBOX_TOUCH_PAD &&
      x < checkboxX + checkboxTouchWidth + CHECKBOX_TOUCH_PAD &&
      y >= checkboxY - CHECKBOX_TOUCH_PAD &&
      y < checkboxY + CHECKBOX_SIZE + CHECKBOX_TOUCH_PAD;
}

///
/// <summary>
/// Returns true if the given touch point falls inside the Restart button.
/// </summary>
/// <param name="x">Touch X coordinate.</param>
/// <param name="y">Touch Y coordinate.</param>
/// <returns>True if the point hits the Restart button.</returns>
///
bool inRestartButton(int16_t x, int16_t y)
{
   return x >= (int16_t)restartButtonRect.left() &&
      x < (int16_t)restartButtonRect.right() &&
      y >= (int16_t)restartButtonRect.top() &&
      y < (int16_t)restartButtonRect.bottom();
}

void setup()
{
   SerialX::begin();

   shouldHangOnThisBoot = readHangFlag();
   sketch.setInitSubheader(
      shouldHangOnThisBoot ? SUBHEADER_HANG : SUBHEADER_NORMAL,
      shouldHangOnThisBoot ? Color::YELLOW : Color::LIGHTGREEN);

   sketch.addSensor("Startup check", initHangTestStep);
   sketch.begin();
   sketch.completeInitialization();

   drawMainDisplay();
}

void loop()
{
   sketch.loop();

   lgfx::touch_point_t touchPoint;
   bool touched = sketch.arduino.display.getTouch(&touchPoint) > 0;

   bool onCheckbox = touched && inCheckbox(touchPoint.x, touchPoint.y);
   if (onCheckbox && !checkboxPressed)
   {
      hangOnNextRestart = !hangOnNextRestart;
      saveHangFlag(hangOnNextRestart);
      drawCheckbox();
   }
   checkboxPressed = onCheckbox;

   bool onRestart = touched && inRestartButton(touchPoint.x, touchPoint.y);
   if (onRestart != restartPressed)
   {
      restartPressed = onRestart;
      drawRestartButton(restartPressed);

      if (!touched && !onRestart)
      {
         Util::reset(0.0f, "Restart button pressed");
      }
   }
}
