//
// Demonstrates the Table family of classes: Table.
//
// Button A / Button B advance / reverse through the available demos. Within a demo,
// Encoder A / Encoder B perform demo-specific actions (e.g. selecting and adjusting a
// field). The screen always shows a "Table" header, a subheading naming the
// current demo, and a bottom instructions line describing what Encoder A/B do.
//
// Demos:
// 1) Live Table    - a plain Table of read-only rows that update every frame.
// 2) Sections      - a Table with section headers and encoder-driven highlighting.
// 3) Tables        - five standalone Table instances positioned at the corners
//                    and center of the display.
//

#include <Wire.h>

#include "ESP32_S3_Playground.h"
#include "SerialX.h"
#include "Table.h"
#include "Util.h"

// ----------- The Board
ESP32_S3_Playground arduino;

// ----------- Layout
constexpr uint8_t HEADER_TEXT_SIZE = 3;
constexpr uint8_t SUBHEADING_TEXT_SIZE = 2;
constexpr uint8_t CONTENT_TEXT_SIZE = 2;
constexpr uint8_t INSTRUCTIONS_TEXT_SIZE = 2;
int16_t headerHeight = 0;
int16_t contentY = 0;
int16_t instructionsY = 0;

// ----------- Demo Selection
enum class Demo : uint8_t { LiveTable, Sections, Tables };
constexpr uint8_t NUM_DEMOS = 3;

struct DemoInfo
{
   const char* name;
   const char* instructions;
};

constexpr DemoInfo DEMOS[NUM_DEMOS] =
{
   { "Live Table", "EncoderA: rate  EncoderB: amplitude" },
   { "Sections", "EncoderA: select row  EncoderB: unused" },
   { "Tables", "EncoderA: select table  EncoderB: adjust value" },
};

Demo currentDemo = Demo::LiveTable;

// ----------- Demo 1: Live Table (plain read-only rows)
constexpr const char* LIVE_RATE_FORMAT = "###/s";
constexpr const char* LIVE_AMPLITUDE_FORMAT = "##.#";
constexpr const char* LIVE_WAVE_FORMAT = "+##.##";
Table* liveTable = nullptr;
long liveRate = 10;
long liveAmplitude = 5;
constexpr long LIVE_RATE_MIN = 1;
constexpr long LIVE_RATE_MAX = 50;
constexpr long LIVE_AMPLITUDE_MIN = 1;
constexpr long LIVE_AMPLITUDE_MAX = 20;
constexpr float LIVE_WAVE_PERIOD_SCALE = 20.0f;

// ----------- Demo 2: Sections (section headers + highlight cycling)
constexpr const char* SECTION_VALUE_FORMAT = "####";
Table* sectionsTable = nullptr;
constexpr uint8_t NUM_SECTION_ROWS = 4;
uint8_t sectionsSelectedRow = 0;
long sectionValues[NUM_SECTION_ROWS] = { 1, 2, 3, 4 };

// ----------- Demo 3: Tables (standalone Table instances at the corners/center)
constexpr const char* TABLES_VALUE_FORMAT = "##.#";
constexpr uint8_t NUM_TABLES = 5;
constexpr float TABLES_VALUE_STEP = 0.1f;
float tablesValues[NUM_TABLES] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
uint8_t tablesSelectedIndex = 0;

Table topLeftTable(&arduino, 0, 0, CONTENT_TEXT_SIZE);
Table topRightTable(&arduino, 0, 0, CONTENT_TEXT_SIZE);
Table bottomRightTable(&arduino, 0, 0, CONTENT_TEXT_SIZE);
Table bottomLeftTable(&arduino, 0, 0, CONTENT_TEXT_SIZE);
Table centerTable(&arduino, 0, 0, CONTENT_TEXT_SIZE);

// Selection rotation order: top-left, top-right, bottom-right, bottom-left, center
Table* tables[NUM_TABLES] =
{
   &topLeftTable, &topRightTable, &bottomRightTable, &bottomLeftTable, &centerTable
};

///
/// <summary>
/// Clears the display and draws the "Table" title plus the current demo's
/// subheading and instructions line, then records the layout coordinates that follow.
/// </summary>
///
void drawHeaderAndFooter()
{
   arduino.clearDisplay();

   arduino.setTextSize(HEADER_TEXT_SIZE);
   arduino.println("Table", Color::HEADING);

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
}

///
/// <summary>
/// Builds the Live Table demo's rows at the shared content position.
/// </summary>
///
void enterLiveTable()
{
   liveTable = new Table(&arduino, 0, contentY, CONTENT_TEXT_SIZE);
   liveTable->addRow("Rate", LIVE_RATE_FORMAT);
   liveTable->addRow("Amplitude", LIVE_AMPLITUDE_FORMAT);
   liveTable->addRow("Wave", LIVE_WAVE_FORMAT);

   for (uint8_t i = 0; i < 3; i++)
   {
      liveTable->setValueBackgroundColor(i, Color::DARKGRAY);
   }
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
   sectionsTable = new Table(&arduino, 0, contentY, CONTENT_TEXT_SIZE);
   sectionsTable->addRow("Row A", SECTION_VALUE_FORMAT);
   sectionsTable->addRow("Row B", SECTION_VALUE_FORMAT);
   sectionsTable->setSection(0, "Group 1");

   sectionsTable->addRow("Row C", SECTION_VALUE_FORMAT);
   sectionsTable->addRow("Row D", SECTION_VALUE_FORMAT);
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
      sectionsTable->setValueBackgroundColor(i, (i == sectionsSelectedRow) ? Color::BLUE : Color::DARKGRAY);
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
/// Builds the Tables demo's five standalone Table instances, each with a single
/// row, and positions them at the top-left, top-right, bottom-right, bottom-left, and
/// center of the display.
/// </summary>
///
void enterTables()
{
   for (uint8_t i = 0; i < NUM_TABLES; i++)
   {
      tables[i]->clearRows();
      tables[i]->addRow("Val", TABLES_VALUE_FORMAT);
      tables[i]->setShowSections(false);
   }

   int16_t tableHeight = tables[0]->getRect().height;

   topLeftTable.setPosition(0, contentY);
   bottomLeftTable.setPosition(0, (int16_t)(arduino.height() - arduino.charH(INSTRUCTIONS_TEXT_SIZE) - tableHeight));

   int16_t topRightWidth = topRightTable.getWidth();
   topRightTable.setPosition((int16_t)(arduino.width() - topRightWidth), contentY);

   int16_t bottomRightWidth = bottomRightTable.getWidth();
   bottomRightTable.setPosition((int16_t)(arduino.width() - bottomRightWidth),
      (int16_t)(arduino.height() - arduino.charH(INSTRUCTIONS_TEXT_SIZE) - tableHeight));

   int16_t centerWidth = centerTable.getWidth();
   centerTable.setPosition((int16_t)(arduino.center().x - centerWidth / 2),
      (int16_t)(arduino.center().y - tableHeight / 2));

   tablesSelectedIndex = 0;
}

///
/// <summary>
/// Sets the selected table's value background to blue and all others to dark gray,
/// then redraws all five tables with their own current values.
/// </summary>
///
void updateTables()
{
   for (uint8_t i = 0; i < NUM_TABLES; i++)
   {
      Color backgroundColor = (i == tablesSelectedIndex) ? Color::BLUE : Color::DARKGRAY;
      tables[i]->setValueBackgroundColor(0, backgroundColor);
      tables[i]->setValue(0, (double)tablesValues[i]);
      tables[i]->draw();
   }
}

///
/// <summary>
/// Applies Encoder A input to cycle the selected table and Encoder B input to adjust the
/// selected table's value.
/// </summary>
///
void handleTablesInput()
{
   int32_t selectDelta = arduino.encoderA.delta();
   if (selectDelta != 0)
   {
      tablesSelectedIndex = (tablesSelectedIndex + NUM_TABLES + selectDelta) % NUM_TABLES;
   }

   int32_t valueDelta = arduino.encoderB.delta();
   if (valueDelta != 0)
   {
      tablesValues[tablesSelectedIndex] += (float)valueDelta * TABLES_VALUE_STEP;
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
   case Demo::Tables:
      enterTables();
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
   case Demo::Tables:
      handleTablesInput();
      updateTables();
      break;
   }
}
