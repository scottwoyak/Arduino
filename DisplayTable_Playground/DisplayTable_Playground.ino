//
// Demonstrates the DisplayTable family of classes: DisplayTable, DisplayTableEditor,
// DisplayTableCellEditor, and DisplayField.
//
// Button A / Button B advance / reverse through the available demos. Within a demo,
// Encoder A / Encoder B perform demo-specific actions (e.g. selecting and adjusting a
// field). The screen always shows a "DisplayTable" header, a subheading naming the
// current demo, and a bottom instructions line describing what Encoder A/B do.
//
// Demos:
// 1) Live Table    - a plain DisplayTable of read-only rows that update every frame.
// 2) Sections      - a DisplayTable with section headers and encoder-driven highlighting.
// 3) Table Editor  - a DisplayTableEditor with mixed editable/read-only fields, showing
//                    selection (Encoder A), adjustment (Encoder B), and Preferences
//                    persistence (Encoder B's button resets to defaults).
// 4) Fields        - standalone DisplayField instances with different alignments.
//

#include <Wire.h>

#include "ESP32_S3_Playground.h"
#include "DisplayField.h"
#include "SerialX.h"
#include "DisplayTable.h"
#include "DisplayTableCellEditor.h"
#include "DisplayTableEditor.h"
#include "Util.h"

// ----------- The Board
ESP32_S3_Playground arduino;

// ----------- Preferences Namespace
constexpr const char* PREF_NAMESPACE = "table_playground";

// ----------- Layout
constexpr uint8_t HEADER_TEXT_SIZE = 3;
constexpr uint8_t SUBHEADING_TEXT_SIZE = 2;
constexpr uint8_t CONTENT_TEXT_SIZE = 2;
constexpr uint8_t INSTRUCTIONS_TEXT_SIZE = 2;
int16_t headerHeight = 0;
int16_t contentY = 0;
int16_t instructionsY = 0;

// ----------- Demo Selection
enum class Demo : uint8_t { LiveTable, Sections, TableEditor, Fields };
constexpr uint8_t NUM_DEMOS = 4;

struct DemoInfo
{
   const char* name;
   const char* instructions;
};

constexpr DemoInfo DEMOS[NUM_DEMOS] =
{
   { "Live Table", "EncoderA: rate  EncoderB: amplitude" },
   { "Sections", "EncoderA: select row  EncoderB: unused" },
   { "Table Editor", "EncoderA: select field  EncoderB: adjust  EncoderB button: reset" },
   { "Fields", "EncoderA: counter  EncoderB: temperature" },
};

Demo currentDemo = Demo::LiveTable;

// ----------- Demo 1: Live Table (plain read-only rows)
Format liveRateFormat("###/s");
Format liveAmplitudeFormat("##.#");
Format liveWaveFormat("+##.##");
DisplayTable* liveTable = nullptr;
long liveRate = 10;
long liveAmplitude = 5;
constexpr long LIVE_RATE_MIN = 1;
constexpr long LIVE_RATE_MAX = 50;
constexpr long LIVE_AMPLITUDE_MIN = 1;
constexpr long LIVE_AMPLITUDE_MAX = 20;
constexpr float LIVE_WAVE_PERIOD_SCALE = 20.0f;

// ----------- Demo 2: Sections (section headers + highlight cycling)
Format sectionValueFormat("####");
DisplayTable* sectionsTable = nullptr;
constexpr uint8_t NUM_SECTION_ROWS = 4;
uint8_t sectionsSelectedRow = 0;
long sectionValues[NUM_SECTION_ROWS] = { 1, 2, 3, 4 };

// ----------- Demo 3: Table Editor (mixed editable/read-only fields)
Format editorRateFormat("###/s");
Format editorModeFormat("######");
Format editorGainFormat("##.##");
Format editorLiveFormat("#####");

long editorRate = 10;
long editorMode = 0;
float editorGain = 1.0f;
float editorLiveValue = 0.0f;

constexpr long EDITOR_RATE_MIN = 1;
constexpr long EDITOR_RATE_MAX = 100;
constexpr long EDITOR_RATE_STEP = 1;
constexpr long EDITOR_RATE_DEFAULT = 10;
constexpr float EDITOR_GAIN_MIN = 0.1f;
constexpr float EDITOR_GAIN_MAX = 10.0f;
constexpr float EDITOR_GAIN_STEP = 0.1f;
constexpr float EDITOR_GAIN_DEFAULT = 1.0f;

const char* const editorModeLabels[] = { "Manual", "Auto", "Timed" };

IntDisplayTableCellEditor editorRateField("Rate", &editorRate,
   EDITOR_RATE_MIN, EDITOR_RATE_MAX, EDITOR_RATE_STEP, EDITOR_RATE_DEFAULT, editorRateFormat);
EnumDisplayTableCellEditor editorModeField("Mode", &editorMode,
   editorModeLabels, ARRAY_SIZE(editorModeLabels), 0, editorModeFormat);
FloatDisplayTableCellEditor editorGainField("Gain", &editorGain,
   EDITOR_GAIN_MIN, EDITOR_GAIN_MAX, EDITOR_GAIN_STEP, EDITOR_GAIN_DEFAULT, editorGainFormat);
ReadOnlyDisplayTableCellEditor editorLiveField("Live", &editorLiveValue, editorLiveFormat);

DisplayTableCellEditor* editorFields[] = { &editorRateField, &editorModeField, &editorGainField, &editorLiveField };
DisplayTableEditor* editorTable = nullptr;

// ----------- Demo 4: Fields (standalone DisplayField instances)
Format fieldCounterFormat("#####");
Format fieldTempFormat("##.# F");
DisplayField* counterField = nullptr;
DisplayField* temperatureField = nullptr;
long fieldCounter = 0;
float fieldTemperature = 72.0f;
constexpr float FIELD_TEMP_STEP_F = 0.5f;

///
/// <summary>
/// Clears the display and draws the "DisplayTable" title plus the current demo's
/// subheading and instructions line, then records the layout coordinates that follow.
/// </summary>
///
void drawHeaderAndFooter()
{
   arduino.clearDisplay();

   arduino.setTextSize(HEADER_TEXT_SIZE);
   arduino.println("DisplayTable", Color::HEADING);

   arduino.setTextSize(SUBHEADING_TEXT_SIZE);
   arduino.println(DEMOS[(uint8_t)currentDemo].name, Color::SUB_HEADING);

   headerHeight = arduino.getCursorY();
   contentY = headerHeight;

   arduino.setTextSize(INSTRUCTIONS_TEXT_SIZE);
   instructionsY = arduino.height() - arduino.charH();
   arduino.setCursor(0, instructionsY);
   arduino.println(DEMOS[(uint8_t)currentDemo].instructions, Color::LABEL);
}

///
/// <summary>
/// Destroys any demo-owned display objects so entering a new demo starts from a clean
/// state. Called before entering a demo and when switching away from one.
/// </summary>
///
void teardownDemos()
{
   delete liveTable;
   liveTable = nullptr;

   delete sectionsTable;
   sectionsTable = nullptr;

   delete editorTable;
   editorTable = nullptr;

   delete counterField;
   counterField = nullptr;

   delete temperatureField;
   temperatureField = nullptr;
}

///
/// <summary>
/// Builds the Live Table demo's rows at the shared content position.
/// </summary>
///
void enterLiveTable()
{
   liveTable = new DisplayTable(&arduino, 0, contentY, CONTENT_TEXT_SIZE);
   liveTable->addRow("Rate", liveRateFormat);
   liveTable->addRow("Amplitude", liveAmplitudeFormat);
   liveTable->addRow("Wave", liveWaveFormat);
}

///
/// <summary>
/// Updates the Live Table demo's values every frame, computing a simple sine wave from
/// the current rate/amplitude settings so the value row visibly animates.
/// </summary>
///
void updateLiveTable()
{
   liveTable->setValue(0, (double)liveRate);
   liveTable->setValue(1, (double)liveAmplitude);

   float periodMs = 1000.0f / (float)liveRate;
   float wave = (float)liveAmplitude * sinf(2.0f * PI * (float)millis() / (periodMs * LIVE_WAVE_PERIOD_SCALE));
   liveTable->setValue(2, (double)wave);
   liveTable->draw();
}

///
/// <summary>
/// Applies Encoder A/B input to the Live Table demo's rate and amplitude settings.
/// </summary>
///
void handleLiveTableInput()
{
   int32_t rateDelta = arduino.encoderA.delta();
   if (rateDelta != 0)
   {
      liveRate = constrain(liveRate + rateDelta, LIVE_RATE_MIN, LIVE_RATE_MAX);
   }

   int32_t amplitudeDelta = arduino.encoderB.delta();
   if (amplitudeDelta != 0)
   {
      liveAmplitude = constrain(liveAmplitude + amplitudeDelta, LIVE_AMPLITUDE_MIN, LIVE_AMPLITUDE_MAX);
   }
}

///
/// <summary>
/// Builds the Sections demo's rows, grouped into two sections, at the shared content
/// position.
/// </summary>
///
void enterSections()
{
   sectionsTable = new DisplayTable(&arduino, 0, contentY, CONTENT_TEXT_SIZE);
   sectionsTable->addRow("Row A", sectionValueFormat);
   sectionsTable->addRow("Row B", sectionValueFormat);
   sectionsTable->setSection(0, "Group 1");

   sectionsTable->addRow("Row C", sectionValueFormat);
   sectionsTable->addRow("Row D", sectionValueFormat);
   sectionsTable->setSection(2, "Group 2");

   sectionsSelectedRow = 0;
}

///
/// <summary>
/// Redraws the Sections demo, highlighting the currently selected row's value background.
/// </summary>
///
void updateSections()
{
   for (uint8_t i = 0; i < NUM_SECTION_ROWS; i++)
   {
      sectionsTable->setValue(i, (double)sectionValues[i]);
      sectionsTable->setValueBackgroundColor(i, (i == sectionsSelectedRow) ? Color::BLUE : Color::BLACK);
   }
   sectionsTable->draw();
}

///
/// <summary>
/// Applies Encoder A input to cycle the Sections demo's highlighted row.
/// </summary>
///
void handleSectionsInput()
{
   int32_t direction = arduino.encoderA.delta();
   if (direction != 0)
   {
      int32_t newRow = ((int32_t)sectionsSelectedRow + (direction > 0 ? 1 : -1) + NUM_SECTION_ROWS) % NUM_SECTION_ROWS;
      sectionsSelectedRow = (uint8_t)newRow;
   }
}

///
/// <summary>
/// Builds the Table Editor demo's DisplayTableEditor and loads its persisted values.
/// </summary>
///
void enterTableEditor()
{
   editorRateField.setSection("Settings");
   editorLiveField.setSection("Measured");

   editorTable = new DisplayTableEditor(&arduino, PREF_NAMESPACE, editorFields, ARRAY_SIZE(editorFields), 0, contentY);
   editorTable->load();
}

///
/// <summary>
/// Updates the Table Editor demo's read-only "Live" row and redraws the table.
/// </summary>
///
void updateTableEditor()
{
   editorLiveValue = (float)editorRate * editorGain;
   editorTable->draw();
}

///
/// <summary>
/// Applies Encoder A/B input to select and adjust the Table Editor demo's fields, and
/// Encoder B's integral button to reset all fields to their defaults.
/// </summary>
///
void handleTableEditorInput()
{
   editorTable->selectNext(arduino.encoderA.delta());

   int32_t adjustDelta = arduino.encoderB.delta();
   if (adjustDelta != 0)
   {
      editorTable->adjustSelected(adjustDelta);
      editorTable->save();
   }

   if (arduino.encoderB.button.wasPressed())
   {
      editorTable->reset();
   }
}

///
/// <summary>
/// Builds the Fields demo's standalone DisplayField instances at the shared content
/// position.
/// </summary>
///
void enterFields()
{
   counterField = new DisplayField(&arduino, Point16(0, contentY), "Counter", fieldCounterFormat);
   temperatureField = new DisplayField(&arduino, Point16(0, (int16_t)(contentY + arduino.charH())),
      "Temperature", fieldTempFormat, Color::LABEL, Color::VALUE, Format::Alignment::LEFT);
}

///
/// <summary>
/// Redraws the Fields demo's counter and temperature values.
/// </summary>
///
void updateFields()
{
   counterField->draw(fieldCounter);
   temperatureField->draw(fieldTemperature);
}

///
/// <summary>
/// Applies Encoder A/B input to the Fields demo's counter and temperature values.
/// </summary>
///
void handleFieldsInput()
{
   fieldCounter += arduino.encoderA.delta();

   int32_t tempDelta = arduino.encoderB.delta();
   if (tempDelta != 0)
   {
      fieldTemperature += (float)tempDelta * FIELD_TEMP_STEP_F;
   }
}

///
/// <summary>
/// Tears down the previous demo's display objects, redraws the chrome for the newly
/// selected demo, and builds that demo's content.
/// </summary>
///
void enterDemo()
{
   teardownDemos();
   drawHeaderAndFooter();

   switch (currentDemo)
   {
   case Demo::LiveTable:
      enterLiveTable();
      break;
   case Demo::Sections:
      enterSections();
      break;
   case Demo::TableEditor:
      enterTableEditor();
      break;
   case Demo::Fields:
      enterFields();
      break;
   }
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();
   arduino.encoderA.begin();
   arduino.encoderB.begin();
   arduino.buttonA.begin();
   arduino.buttonB.begin();

   enterDemo();
}

void loop()
{
   if (arduino.buttonA.wasPressed())
   {
      currentDemo = (Demo)(((uint8_t)currentDemo + 1) % NUM_DEMOS);
      enterDemo();
   }
   else if (arduino.buttonB.wasPressed())
   {
      currentDemo = (Demo)(((uint8_t)currentDemo + NUM_DEMOS - 1) % NUM_DEMOS);
      enterDemo();
   }

   switch (currentDemo)
   {
   case Demo::LiveTable:
      handleLiveTableInput();
      updateLiveTable();
      break;
   case Demo::Sections:
      handleSectionsInput();
      updateSections();
      break;
   case Demo::TableEditor:
      handleTableEditorInput();
      updateTableEditor();
      break;
   case Demo::Fields:
      handleFieldsInput();
      updateFields();
      break;
   }
}
