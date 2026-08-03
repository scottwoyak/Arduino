//
// Color Playground: browses every named web/CSS color defined in Color.h in a grid,
// sized at runtime to fit as many 30px color squares as the display allows.
//
// Use encoderA to move the selection left/right and encoderB to move it up/down. The
// selected cell is outlined with a black pixel ring followed by a white pixel ring,
// drawn just outside the cell. The name, RGB, and HSV values of the selected color are
// shown in a table centered in the space below the grid. Pressing encoderA's button
// cycles the sort order (Name/Hue/Saturation/Value/Luminance); pressing encoderB's
// button reverses the current sort order.
//

// Local library headers (from libraries/Woyak)
// ESP32_S3_Playground.h must come first so LGFX/LGFX_Sprite are defined before any
// header that transitively includes ArduinoWithDisplay.h.
#include "ESP32_S3_Playground.h"
#include "SerialX.h"
#include "Util.h"
#include "ArduinoBoard.h"
#include "FieldTable.h"
#include "ValueEditor.h"

#include <algorithm>
#include <cmath>
#include <string>

constexpr uint8_t CONTENT_TEXT_SIZE = 3;
constexpr uint8_t SUBHEADING_TEXT_SIZE = 2;
constexpr int16_t CELL_GAP = 3; // pixels between adjacent grid squares
constexpr int16_t CELL_SIZE = 20; // pixels, width/height of each color square
constexpr int16_t HEADING_MARGIN = 8; // pixels between the heading and the grid, and between the grid and the info table
constexpr int16_t SELECTION_OUTLINE_MARGIN = 2; // pixels the selection outline is drawn outside a selected cell

// ----------- The Board
ESP32_S3_Playground arduino;

// ColorEntry, COLORS, and NUM_COLORS are defined in Color.h so they can be shared with
// other sketches (e.g. Color_Calibrator_Playground).

int16_t gridLeft = 0;
int16_t gridTop = 0;
int16_t subheadingTop = 0;
int16_t cellSize = CELL_SIZE;
uint8_t gridColumns = 0;
uint8_t gridRows = 0;
uint8_t selectedColumn = 0;
uint8_t selectedRow = 0;

// ----------- Selected color swatch, shown to the left of the info table
int16_t swatchLeft = 0;
int16_t swatchTop = 0;

// ----------- Selected color info (name/RGB/HSV), shown as a centered table below the grid
StringValue selectedNameValue("####################"); // 20 chars: LIGHTGOLDENRODYELLOW
StringValue selectedRGBValue("##############"); // 14 chars: 255, 255, 255
StringValue selectedHSVValue("###############"); // 15 chars: 360, 100%, 100%
FieldTable::Row infoRows[] =
{
   FieldTable::Row("Name", &selectedNameValue),
   FieldTable::Row("RGB", &selectedRGBValue),
   FieldTable::Row("HSV", &selectedHSVValue),
};
FieldTable infoTable(&arduino, 0, 0, infoRows, CONTENT_TEXT_SIZE);

// ----------- Sorting: cycles through Name/Hue/Saturation/Value/Luminance order via
// encoderA's button; encoderB's button reverses the current order.
enum class SortMode
{
   NAME,
   HUE,
   SATURATION,
   VALUE,
   LUMINANCE,
};
constexpr SortMode SORT_MODES[] = { SortMode::NAME, SortMode::HUE, SortMode::SATURATION, SortMode::VALUE, SortMode::LUMINANCE };
constexpr uint8_t NUM_SORT_MODES = sizeof(SORT_MODES) / sizeof(SORT_MODES[0]);
uint8_t sortModeIndex = 0;

// Reverses the current sort order (see applySortMode()), toggled via encoderB's button.
bool sortReversed = false;

// Cached HSV components for every entry in COLORS, indexed the same way, so re-sorting
// doesn't need to recompute HSV from RGB each time.
float colorHue[NUM_COLORS];
float colorSaturation[NUM_COLORS];
float colorValue[NUM_COLORS];

// Cached perceived brightness (Rec. 709 luma: 0.2126R + 0.7152G + 0.0722B, each 0.0-1.0),
// indexed the same way as COLORS, for SortMode::LUMINANCE - matches the luma formula
// Color565::fromHSVLuminance() targets, so this groups colors by the same notion of
// perceived brightness used elsewhere in the codebase, rather than by HSV "value" (which
// SortMode::VALUE already covers and which doesn't account for hue's effect on perceived
// brightness).
float colorLuminance[NUM_COLORS];

// Maps a row-major grid position (see colorIndexAt()) to the COLORS index that should be
// displayed there under the current sort mode. Rebuilt by applySortMode() whenever the
// sort mode changes.
size_t sortedIndices[NUM_COLORS];

///
/// <summary>
/// Converts a Color to its hue/saturation/value components, each normalized to 0.0-1.0,
/// for use as a sort key that groups visually similar colors together.
/// </summary>
/// <param name="color">The color to convert.</param>
/// <param name="h">Receives the hue (0.0-1.0).</param>
/// <param name="s">Receives the saturation (0.0-1.0).</param>
/// <param name="v">Receives the value/brightness (0.0-1.0).</param>
///
void toHSV(Color color, float& h, float& s, float& v)
{
   float r = Color565::getR(color) / 255.0f;
   float g = Color565::getG(color) / 255.0f;
   float b = Color565::getB(color) / 255.0f;

   float maxC = std::max(r, std::max(g, b));
   float minC = std::min(r, std::min(g, b));
   float delta = maxC - minC;

   v = maxC;

   if (delta < 0.0001f)
   {
      // Hue is undefined for achromatic colors (black/white/gray shades). Use a sentinel
      // value distinct from 0.0 (true red) so these colors don't collide with red hues
      // when sorting; they instead sort as a separate group (see applySortMode()).
      h = -1.0f;
      s = 0.0f;
      return;
   }

   s = (maxC < 0.0001f) ? 0.0f : (delta / maxC);

   if (maxC == r)
   {
      h = (g - b) / delta + (g < b ? 6.0f : 0.0f);
   }
   else if (maxC == g)
   {
      h = (b - r) / delta + 2.0f;
   }
   else
   {
      h = (r - g) / delta + 4.0f;
   }
   h /= 6.0f;
}

///
/// <summary>
/// Computes a Color's perceived brightness (Rec. 709 luma: 0.2126R + 0.7152G + 0.0722B),
/// normalized to 0.0-1.0, for use as a sort key - see colorLuminance's declaration.
/// </summary>
/// <param name="color">The color to convert.</param>
/// <returns>The perceived brightness (0.0-1.0).</returns>
///
float toLuminance(Color color)
{
   float r = Color565::getR(color) / 255.0f;
   float g = Color565::getG(color) / 255.0f;
   float b = Color565::getB(color) / 255.0f;

   return (0.2126f * r) + (0.7152f * g) + (0.0722f * b);
}

// Grid cells are laid out in row-major order (left to right across row 0, then left to
// right across row 1, etc.), with the COLORS entry shown at each position determined by
// sortedIndices (see applySortMode()) rather than COLORS order directly, so re-sorting
// only needs to rebuild sortedIndices. gridColumns/gridRows are computed at runtime (see
// setup()) to fit the display; cells past NUM_COLORS (or past gridColumns * gridRows) are
// left empty (black).

///
/// <summary>
/// Maps a grid (column, row) position to the corresponding COLORS index, or -1 if that
/// cell is past the end of the color list.
/// </summary>
/// <param name="column">Grid column (0-based).</param>
/// <param name="row">Grid row (0-based).</param>
/// <returns>The color index, or -1 if the cell is empty.</returns>
///
long colorIndexAt(uint8_t column, uint8_t row)
{
   long rawIndex = (long)row * gridColumns + column;
   return (rawIndex < (long)NUM_COLORS) ? (long)sortedIndices[rawIndex] : -1;
}

///
/// <summary>
/// Gets the last row in the given column that holds a color (i.e. the bottommost row for
/// which colorIndexAt(column, row) is valid), so vertical movement can avoid landing on an
/// empty cell in a partially-filled trailing row.
/// </summary>
/// <param name="column">Grid column (0-based).</param>
/// <returns>The last populated row for that column.</returns>
///
uint8_t lastValidRow(uint8_t column)
{
   uint8_t row = gridRows - 1;
   while (row > 0 && colorIndexAt(column, row) < 0)
   {
      row--;
   }
   return row;
}

///
/// <summary>
/// Gets the display label for a sort mode, used for the subheading.
/// </summary>
/// <param name="mode">The sort mode.</param>
/// <returns>A short human-readable label.</returns>
///
const char* sortModeLabel(SortMode mode)
{
   switch (mode)
   {
      case SortMode::NAME: return "Sorted by Name";
      case SortMode::HUE: return "Sorted by Hue";
      case SortMode::SATURATION: return "Sorted by Saturation";
      case SortMode::LUMINANCE: return "Sorted by Luminance";
      case SortMode::VALUE: return "Sorted by Value";
      default: return "";
   }
}

///
/// <summary>
/// Rebuilds sortedIndices for the current sort mode, using the cached HSV components
/// (colorHue/colorSaturation/colorValue) so no per-color HSV conversion is needed here.
/// COLORS is already alphabetical, so SortMode::NAME is simply the identity mapping.
/// </summary>
///
void applySortMode()
{
   for (size_t i = 0; i < NUM_COLORS; i++)
   {
      sortedIndices[i] = i;
   }

   SortMode mode = SORT_MODES[sortModeIndex];
   if (mode != SortMode::NAME)
   {
      std::stable_sort(sortedIndices, sortedIndices + NUM_COLORS, [mode](size_t a, size_t b)
      {
         switch (mode)
         {
            case SortMode::HUE:
            {
               // Achromatic colors (sentinel hue -1.0) have no true hue; group them together
               // after all chromatic colors, ordered by brightness, instead of mixing them in
               // with true hue-0 (red) colors.
               bool achromaticA = colorHue[a] < 0.0f;
               bool achromaticB = colorHue[b] < 0.0f;
               if (achromaticA != achromaticB)
               {
                  return achromaticB; // chromatic (a) sorts before achromatic (b)
               }
               if (achromaticA)
               {
                  return colorValue[a] < colorValue[b];
               }
               return colorHue[a] < colorHue[b];
            }
            case SortMode::SATURATION: return colorSaturation[a] < colorSaturation[b];
            case SortMode::VALUE: return colorValue[a] < colorValue[b];
            case SortMode::LUMINANCE: return colorLuminance[a] < colorLuminance[b];
            default: return false;
         }
      });
   }

   if (sortReversed)
   {
      std::reverse(sortedIndices, sortedIndices + NUM_COLORS);
   }
}

///
/// <summary>
/// Draws a single grid cell's color square. If it is the currently selected cell, also
/// draws a selection outline just outside the square: a black pixel ring followed by a
/// white pixel ring, using part of the gap between cells.
/// </summary>
/// <param name="column">Grid column (0-based).</param>
/// <param name="row">Grid row (0-based).</param>
///
void drawCell(uint8_t column, uint8_t row)
{
   int16_t x = gridLeft + column * (cellSize + CELL_GAP);
   int16_t y = gridTop + row * (cellSize + CELL_GAP);

   long index = colorIndexAt(column, row);
   Color color = (index >= 0) ? COLORS[index].color : Color::BLACK;

   arduino.fillRect(x, y, cellSize, cellSize, color);

   bool selected = (column == selectedColumn && row == selectedRow);
   arduino.display.drawRect(x - SELECTION_OUTLINE_MARGIN, y - SELECTION_OUTLINE_MARGIN, cellSize + 2 * SELECTION_OUTLINE_MARGIN, cellSize + 2 * SELECTION_OUTLINE_MARGIN, (uint16_t)Color::BLACK);
   arduino.display.drawRect(x - 1, y - 1, cellSize + 2, cellSize + 2, (uint16_t)(selected ? Color::WHITE : Color::BLACK));
}

///
/// <summary>
/// Draws every cell in the grid.
/// </summary>
///
void drawGrid()
{
   for (uint8_t column = 0; column < gridColumns; column++)
   {
      for (uint8_t row = 0; row < gridRows; row++)
      {
         drawCell(column, row);
      }
   }
}

///
/// <summary>
/// Redraws just the previously and newly selected cells, so moving the selection doesn't
/// require repainting the whole grid.
/// </summary>
/// <param name="prevColumn">Previously selected column.</param>
/// <param name="prevRow">Previously selected row.</param>
///
void moveSelection(uint8_t prevColumn, uint8_t prevRow)
{
   drawCell(prevColumn, prevRow);
   drawCell(selectedColumn, selectedRow);
}

///
/// <summary>
/// Draws the subheading naming the current sort mode (e.g. "Sorted by Name"), first
/// clearing the full line so a shorter label doesn't leave remnants of a longer previous
/// one (e.g. "Sorted by Saturation" -> "Sorted by Hue").
/// </summary>
///
void drawSubheading()
{
   arduino.setTextSize(SUBHEADING_TEXT_SIZE);
   arduino.fillRect(0, subheadingTop, arduino.width(), arduino.charH(SUBHEADING_TEXT_SIZE), Color::BLACK);
   arduino.setCursor(0, subheadingTop);
   String label = sortModeLabel(SORT_MODES[sortModeIndex]);
   if (sortReversed)
   {
      label += " (Reversed)";
   }
   arduino.println(label, Color::SUB_HEADING);
}

///
/// <summary>
/// Draws the currently selected color as a filled square to the left of the info
/// table, matching the table's height.
/// </summary>
///
void drawSwatch()
{
   long index = colorIndexAt(selectedColumn, selectedRow);
   Color color = (index >= 0) ? COLORS[index].color : Color::BLACK;

   int16_t swatchSize = infoTable.getHeight();
   arduino.fillRect(swatchLeft, swatchTop, swatchSize, swatchSize, color);
}

///
/// <summary>
/// Updates the name, RGB, and HSV values of the currently selected color and redraws
/// the info table (only rows whose text actually changed are repainted).
/// </summary>
///
void drawInfo()
{
   long index = colorIndexAt(selectedColumn, selectedRow);
   if (index < 0)
   {
      selectedNameValue.set("");
      selectedRGBValue.set("");
      selectedHSVValue.set("");
      drawSwatch();
      infoTable.draw();
      return;
   }

   const ColorEntry& entry = COLORS[index];

   uint8_t r = Color565::getR(entry.color);
   uint8_t g = Color565::getG(entry.color);
   uint8_t b = Color565::getB(entry.color);

   float h, s, v;
   toHSV(entry.color, h, s, v);

   char rgbText[24];
   snprintf(rgbText, sizeof(rgbText), "%d, %d, %d", r, g, b);

   char hsvText[24];
   snprintf(hsvText, sizeof(hsvText), "%d, %d%%, %d%%",
      (int)std::lround(h * 360.0f), (int)std::lround(s * 100.0f), (int)std::lround(v * 100.0f));

   selectedNameValue.set(entry.name);
   selectedRGBValue.set(rgbText);
   selectedHSVValue.set(hsvText);

   drawSwatch();
   infoTable.draw();
}

void setup()
{
   SerialX::begin();
   arduino.begin();

   for (size_t i = 0; i < NUM_COLORS; i++)
   {
      toHSV(COLORS[i].color, colorHue[i], colorSaturation[i], colorValue[i]);
      colorLuminance[i] = toLuminance(COLORS[i].color);
   }
   applySortMode();

   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.println("Colors", Color::HEADING);

   subheadingTop = arduino.getCursorY();
   drawSubheading();

   int16_t headingBottom = arduino.getCursorY();
   int16_t infoHeight = infoTable.getHeight();

   // Figure out how many columns/rows of CELL_SIZE boxes (plus CELL_GAP between them) fit
   // in the space available for the grid, leaving room for the heading and subheading
   // above and the info table (plus a margin above it) below. SELECTION_OUTLINE_MARGIN is
   // reserved on both sides so the selection outline (drawn 2px outside the selected cell,
   // see drawCell()) never falls off-screen for cells in the leftmost/rightmost column.
   int16_t availableWidth = (int16_t)arduino.width() - 2 * SELECTION_OUTLINE_MARGIN;
   int16_t availableHeight = (int16_t)arduino.height() - headingBottom - 2 * HEADING_MARGIN - infoHeight;

   gridColumns = (uint8_t)((availableWidth + CELL_GAP) / (cellSize + CELL_GAP));
   gridRows = (uint8_t)((availableHeight + CELL_GAP) / (cellSize + CELL_GAP));

   size_t numCells = (size_t)gridColumns * gridRows;
   if (numCells < NUM_COLORS)
   {
      Util::setHaltReason("Display too small to show every color");
      Util::reset();
   }

   int16_t gridWidth = gridColumns * (cellSize + CELL_GAP) - CELL_GAP;
   int16_t gridHeight = gridRows * (cellSize + CELL_GAP) - CELL_GAP;

   gridLeft = (int16_t)((arduino.width() - gridWidth) / 2);
   gridTop = headingBottom + HEADING_MARGIN;

   int16_t gridBottom = gridTop + gridHeight;
   int16_t infoAreaTop = gridBottom + HEADING_MARGIN;
   int16_t infoAreaBottom = (int16_t)arduino.height();

   // FieldTable reserves one character width of invisible padding to the left of its
   // rows' actual label text (see FieldTable::_relayout()), which getWidth() includes.
   // Strip it out here so the swatch and the table's visible content are centered as a
   // single group, rather than the table's true content appearing shifted right of it.
   int16_t charW = arduino.charW(CONTENT_TEXT_SIZE);
   int16_t infoTableWidth = infoTable.getWidth() - charW;
   int16_t swatchGap = charW;
   int16_t swatchSize = infoHeight;
   int16_t groupWidth = swatchSize + swatchGap + infoTableWidth;

   swatchLeft = (int16_t)((arduino.width() - groupWidth) / 2);
   swatchTop = (int16_t)((infoAreaTop + infoAreaBottom - infoHeight) / 2);

   int16_t infoTableLeft = swatchLeft + swatchSize + swatchGap - charW;

   infoTable.setPosition(infoTableLeft, swatchTop, Anchor::TOP_LEFT);

   drawGrid();
   drawInfo();

   arduino.encoderA.reset();
   arduino.encoderB.reset();
}

void loop()
{
   // wasPressed() consumes the press, so each button is polled exactly once and its
   // result cached, rather than re-checked (which would always read false the second
   // time) or skipped via short-circuiting.
   bool sortModePressed = arduino.encoderA.button.wasPressed();
   bool sortReversedPressed = arduino.encoderB.button.wasPressed();
   if (sortModePressed || sortReversedPressed)
   {
      // Preserve the currently selected color (not just its grid position) across the
      // resort, since the same COLORS entry generally lands in a different cell under a
      // new sort mode/order.
      long selectedColorIndex = colorIndexAt(selectedColumn, selectedRow);

      if (sortModePressed)
      {
         sortModeIndex = (uint8_t)((sortModeIndex + 1) % NUM_SORT_MODES);
      }
      if (sortReversedPressed)
      {
         sortReversed = !sortReversed;
      }
      applySortMode();

      if (selectedColorIndex >= 0)
      {
         for (size_t i = 0; i < NUM_COLORS; i++)
         {
            if (sortedIndices[i] == (size_t)selectedColorIndex)
            {
               selectedColumn = (uint8_t)(i % gridColumns);
               selectedRow = (uint8_t)(i / gridColumns);
               break;
            }
         }
      }

      drawSubheading();
      drawGrid();
      drawInfo();
   }

   if (arduino.encoderA.hasChanged() || arduino.encoderB.hasChanged())
   {
      int32_t deltaA = arduino.encoderA.delta();
      int32_t deltaB = arduino.encoderB.delta();

      uint8_t prevColumn = selectedColumn;
      uint8_t prevRow = selectedRow;

      // encoderA moves left/right, wrapping into the next/previous row when it runs
      // past the end/start of the current row (row-major order, wrapping at the grid's
      // total cell count).
      long numCells = (long)gridColumns * gridRows;
      long index = (long)selectedRow * gridColumns + selectedColumn;
      index = ((index + deltaA) % numCells + numCells) % numCells;
      selectedColumn = (uint8_t)(index % gridColumns);
      selectedRow = (uint8_t)(index / gridColumns);

      // encoderB moves up/down, wrapping within only the rows that actually hold a color
      // in this column, so the selection never lands on an empty trailing cell (columns
      // past the last full row have fewer populated rows than gridRows).
      uint8_t maxRow = lastValidRow(selectedColumn);
      selectedRow = (uint8_t)((selectedRow + deltaB + maxRow + 1) % (maxRow + 1));

      moveSelection(prevColumn, prevRow);
      drawInfo();
   }
}
