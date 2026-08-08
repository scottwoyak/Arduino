//
// LED Calibrator Playground: finds per-LED calibration factors so different LEDs
// (with different forward voltages/resistors) appear equally bright at the same level.
//
// A Table lists one row per LED with columns for its GPIO pin, on/off state, and
// calibration factor, plus a final "Overall" row holding the shared level applied to
// every LED. Encoder A cycles the selected row, Encoder B adjusts the selected row's
// factor (or the Overall row's level), and Encoder B's integral button toggles the
// selected LED's on/off state (the Overall row has no on/off state and ignores the
// button). Once a LED's calibration factor is set, its perceived brightness should
// track the Overall level the same as all the other LEDs. Factors and the overall
// level are persisted to Preferences as they change and reloaded automatically on
// startup.
//
// Hardware: ESP32-S3 Dev Module wired as a Playground board (TFT display + rotary
// encoders), plus 4 LEDs (with current-limiting resistors) wired to GPIO 17, 41, 42,
// and 47 (free pins on this board's Playground wiring, avoiding Capacitor_Playground's
// resistor charge pins). Adjust LED_PIN_* below if your wiring differs.
//

#include <Arduino.h>
#include <string>

#include "ESP32_S3_Playground.h"
#include "ArduinoBoard.h"
#include "Table.h"
#include "LED.h"
#include "SerialX.h"

// ----------- The Board
ESP32_S3_Playground arduino;

// ----------- Text Sizes
constexpr uint8_t HEADING_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t CONTENT_TEXT_SIZE = DEFAULT_CONTENT_SIZE;

// ----------- Preferences Namespace
constexpr const char* PREF_NAMESPACE = "LedCalibratorPg";

// ----------- LED Wiring
// Chosen to avoid the ESP32-S3 Dev Module Playground's other peripherals: the TFT
// (5, 6, 9/14, 10-13), default I2C (8, 9), encoders (3, 4, 18, 38-40), standalone
// buttons (21, 46), the onboard NeoPixel (48), and Capacitor_Playground's
// CapacitorSensor pins (SENSE_PIN=7, CHARGE_PIN_47K=1, CHARGE_PIN_100K=2,
// CHARGE_PIN_1M=15, CHARGE_PIN_470K=16). Adjust if your wiring differs.
constexpr uint8_t LED_PIN_1 = 17;
constexpr uint8_t LED_PIN_2 = 41;
constexpr uint8_t LED_PIN_3 = 42;
constexpr uint8_t LED_PIN_4 = 47;
constexpr uint8_t LED_PINS[] = { LED_PIN_1, LED_PIN_2, LED_PIN_3, LED_PIN_4 };

BasicLED led1(LED_PIN_1);
BasicLED led2(LED_PIN_2);
BasicLED led3(LED_PIN_3);
BasicLED led4(LED_PIN_4);
BasicLED* const leds[] = { &led1, &led2, &led3, &led4 };
constexpr uint8_t CALIBRATOR_NUM_LEDS = sizeof(leds) / sizeof(leds[0]);

// ----------- Calibration Factor Range/Step (0.0-1.0 in 0.05 steps)
constexpr float MIN_FACTOR = 0.0f;
constexpr float MAX_FACTOR = 1.0f;
constexpr float FACTOR_STEP = 0.05f;
constexpr float DEFAULT_FACTOR = 1.0f;

// ----------- Overall Level Range/Step (0-100% in 5% steps)
constexpr long MIN_LEVEL_PERCENT = 0;
constexpr long MAX_LEVEL_PERCENT = 100;
constexpr long LEVEL_STEP_PERCENT = 5;
constexpr long DEFAULT_LEVEL_PERCENT = 100;

// ----------- Live State (edited via the encoders, applied to the LEDs every loop)
bool ledOn[CALIBRATOR_NUM_LEDS] = { true, true, true, true };
float ledFactor[CALIBRATOR_NUM_LEDS] = { DEFAULT_FACTOR, DEFAULT_FACTOR, DEFAULT_FACTOR, DEFAULT_FACTOR };
long overallLevelPercent = DEFAULT_LEVEL_PERCENT;

// Selectable rows: one per LED, plus a final "Overall" row.
constexpr uint8_t NUM_SELECTABLE_ROWS = CALIBRATOR_NUM_LEDS + 1;
constexpr uint8_t OVERALL_ROW_INDEX = CALIBRATOR_NUM_LEDS;
uint8_t selectedRowIndex = 0;

// ----------- Table Columns: Pin, State, Factor
Table::Column COLUMNS[] = {
   { "LED" },
   { "Pin", "###", Table::Alignment::RIGHT },
   { "State", "#####", Table::Alignment::RIGHT },
   { "Factor", "#.##", Table::Alignment::RIGHT },
};
Table::Row ROWS[] = {
   { "1" },
   { "2" },
   { "3" },
   { "4" },
   { "Overall" },
};
Table table(&arduino, 0, 0, COLUMNS, ROWS, CONTENT_TEXT_SIZE);

///
/// <summary>
/// Generates the Preferences key used to persist a given LED's calibration factor,
/// e.g. "factor0".
/// </summary>
/// <param name="ledIndex">Zero-based LED index.</param>
/// <returns>The Preferences key for that LED's calibration factor.</returns>
///
std::string factorKey(uint8_t ledIndex)
{
   return "factor" + std::to_string(ledIndex);
}

///
/// <summary>
/// Loads every LED's persisted calibration factor and the overall level from
/// Preferences, falling back to DEFAULT_FACTOR/DEFAULT_LEVEL_PERCENT for any value
/// with no saved entry.
/// </summary>
///
void loadSettings()
{
   arduino.preferences.begin(PREF_NAMESPACE, true);

   for (uint8_t i = 0; i < CALIBRATOR_NUM_LEDS; i++)
   {
      ledFactor[i] = arduino.preferences.getFloat(factorKey(i).c_str(), DEFAULT_FACTOR);
   }
   overallLevelPercent = arduino.preferences.getUInt("overall", (uint32_t)DEFAULT_LEVEL_PERCENT);

   arduino.preferences.end();
}

///
/// <summary>
/// Persists the given LED's current calibration factor to Preferences.
/// </summary>
/// <param name="ledIndex">Zero-based LED index.</param>
///
void saveFactor(uint8_t ledIndex)
{
   arduino.preferences.begin(PREF_NAMESPACE, false);
   arduino.preferences.putFloat(factorKey(ledIndex).c_str(), ledFactor[ledIndex]);
   arduino.preferences.end();
}

///
/// <summary>
/// Persists the current overall level to Preferences.
/// </summary>
///
void saveOverallLevel()
{
   arduino.preferences.begin(PREF_NAMESPACE, false);
   arduino.preferences.putUInt("overall", (uint32_t)overallLevelPercent);
   arduino.preferences.end();
}

///
/// <summary>
/// Refreshes every row's Pin/State/Factor cells from the live LED state, plus the
/// final Overall row's Factor cell showing the shared level, highlighting the
/// currently selected row's last cell with a blue background and white text, matching
/// the other editor controls' selection style.
/// </summary>
///
void updateTable()
{
   for (uint8_t i = 0; i < CALIBRATOR_NUM_LEDS; i++)
   {
      bool isSelected = (i == selectedRowIndex);
      Color valueColor = isSelected ? Color::WHITE : Color::VALUE;
      Color backgroundColor = isSelected ? Color::BLUE : Color::BLACK;

      table.setValue(i, 0, LED_PINS[i]);
      table.setValue(i, 1, ledOn[i] ? "ON" : "OFF");
      table.setValue(i, 2, ledFactor[i], valueColor);
      table.setValueBackgroundColor(i, 2, backgroundColor);
   }

   bool isOverallSelected = (selectedRowIndex == OVERALL_ROW_INDEX);
   Color overallValueColor = isOverallSelected ? Color::WHITE : Color::VALUE;
   Color overallBackgroundColor = isOverallSelected ? Color::BLUE : Color::BLACK;
   table.setValue(OVERALL_ROW_INDEX, 2, overallLevelPercent / 100.0f, overallValueColor);
   table.setValueBackgroundColor(OVERALL_ROW_INDEX, 2, overallBackgroundColor);
}

///
/// <summary>
/// Applies each LED's current level (overall level scaled by its calibration factor)
/// and on/off state to its PWM output. turnOn()/turnOff() are called (rather than just
/// setLevel()) since they're what actually reapply the pin's PWM output with the LED's
/// current level.
/// </summary>
///
void applyLevels()
{
   for (uint8_t i = 0; i < CALIBRATOR_NUM_LEDS; i++)
   {
      leds[i]->setCalibrationFactor(ledFactor[i]);
      leds[i]->setLevel(overallLevelPercent / 100.0f);

      if (ledOn[i])
      {
         leds[i]->turnOn();
      }
      else
      {
         leds[i]->turnOff();
      }
   }
}

void setup()
{
   SerialX::begin();
   arduino.begin();

   for (uint8_t i = 0; i < CALIBRATOR_NUM_LEDS; i++)
   {
      leds[i]->begin();
      leds[i]->turnOn();
   }

   loadSettings();

   arduino.setCursor(0, 0);
   arduino.setTextSize(HEADING_SIZE);
   arduino.println("LED Calibrator", Color::HEADING);

   int16_t contentY = arduino.getCursorY();
   int16_t bottom = (int16_t)arduino.height();
   table.setPosition((int16_t)arduino.width() / 2, (contentY + bottom) / 2, Anchor::CENTER);
   updateTable();
   table.draw();

   applyLevels();
}

void loop()
{
   if (arduino.encoderA.hasChanged())
   {
      int32_t selectDelta = arduino.encoderA.delta();
      int32_t step = selectDelta > 0 ? 1 : NUM_SELECTABLE_ROWS - 1;
      for (int32_t i = 0; i < abs(selectDelta); i++)
      {
         selectedRowIndex = (selectedRowIndex + step) % NUM_SELECTABLE_ROWS;
      }
      updateTable();
   }

   if (arduino.encoderB.hasChanged())
   {
      int32_t adjustDelta = arduino.encoderB.delta();

      if (selectedRowIndex == OVERALL_ROW_INDEX)
      {
         long newLevel = overallLevelPercent - adjustDelta * LEVEL_STEP_PERCENT;
         overallLevelPercent = constrain(newLevel, MIN_LEVEL_PERCENT, MAX_LEVEL_PERCENT);
         saveOverallLevel();
      }
      else
      {
         float newFactor = ledFactor[selectedRowIndex] - adjustDelta * FACTOR_STEP;
         ledFactor[selectedRowIndex] = constrain(newFactor, MIN_FACTOR, MAX_FACTOR);
         saveFactor(selectedRowIndex);
      }

      updateTable();
   }

   if (arduino.encoderB.button.wasPressed() && selectedRowIndex != OVERALL_ROW_INDEX)
   {
      ledOn[selectedRowIndex] = !ledOn[selectedRowIndex];
      updateTable();
   }

   table.draw();
   applyLevels();
}
