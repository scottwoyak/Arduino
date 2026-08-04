//
// Color Calibrator Playground: six views, paged back and forth with Button A (next) /
// Button B (previous), same interaction pattern as Table_Playground.
//
// Views 1, 2, and 4 ("Hues by Value", "Hues by Lightness", "Hues by Saturation") each show
// a single grid of color bars, laid out left to right in ascending hue order (no sorting/
// weighting/RGB controls - these views are just about seeing the spectrum of hues). Each
// bar column is a stack of blocks showing the same hue at increasing HSV Value / HSL
// Lightness / HSL Saturation (respectively) top to bottom. Above the grid, a matching stack
// of marker rows (one per level, same order) shows only a single colored pixel in each
// level's color, horizontally centered in the column, against an otherwise black
// background - making it easier to pick out a single column's hue when columns are only a
// couple of pixels wide. Below the grid is a one-row table for the shared HSL/HSV
// component not varied by row (Saturation for "Hues by Value"/"Hues by Lightness",
// Lightness for "Hues by Saturation"), editable live with Encoder B (Encoder A just
// selects the field, since it's the only one). All three views share their grid/field
// layout and draw/change-tracking logic via the HueGrid class; only their per-column color
// formula differs.
//
// View 3 "Hues by Luminance" mirrors the Hues by Lightness view, but instead of using each
// row's value directly as the HSL lightness, treats it as a target perceived luminance and
// solves for the lightness that achieves it at each hue, using the shared Weighting/Red/
// Green/Blue coefficients from the Luminance view's calibration table - so every bar in a
// given row reads as equally bright regardless of hue.
//
// View 5 "Luminance" - a single row of narrow (5 pixel wide) color bars generated from a
// spread of hue, saturation, and lightness combinations (see generateLuminancePalette()),
// packed as densely as possible across the display so many combinations can be compared at
// once, sorted by perceived brightness using the calibration table below (lightest on the
// left). A matching row of marker dots is shown above the bars, same idea as the Hues
// views' marker rows. Below the row is a table, editable live with the encoders (same
// interaction pattern as Basic_FieldTableEditor): Encoder A selects a field, Encoder B
// adjusts it (Encoder B's integral button resets all fields to their defaults). Encoder
// A's integral button reverses the sort order, the same way Color_Playground reverses its
// own sort order. To the right of the editable table is a read-only reference table
// listing well-known luminance formulas' coefficients.
//
// The table's Red/Green/Blue fields are the luminance formula's coefficients (adjusted in
// steps of 0.01), applied identically to every bar. Its "Weighting" field also applies
// uniformly: the coefficients answer "how much does each channel contribute to
// brightness," while Weighting answers "how does that numeric result relate to what a
// human actually perceives" - plain gamma-encoded channel values aren't perceptually
// linear (e.g. 50% isn't half as bright as 100%). "None" sums the raw channels as-is;
// "Gamma-corrected" decodes sRGB gamma to linear light before summing; "HSP" instead sums
// the weighted squares of the raw channels (then takes the square root), which - unlike
// "Gamma-corrected" - can actually reorder colors differently than "None".
//
// View 6 "Recommended Plotting Colors" - two side-by-side sets of colors, each sharing the
// same evenly spread hues but generated from its own editable target luminance and chroma,
// plus a fixed reference set of primary/secondary colors, so recommended plotting palettes
// can be compared side by side (see the section comment below for details).
//


// Local library headers (from libraries/Woyak)
// ESP32_S3_Playground.h must come first so LGFX/LGFX_Sprite are defined before any
// header that transitively includes ArduinoWithDisplay.h.
#include "ESP32_S3_Playground.h"
#include "SerialX.h"
#include "Util.h"
#include "ArduinoBoard.h"
#include "FieldTable.h"
#include "FieldTableEditor.h"
#include "FieldEditor.h"
#include "ValueEditor.h"
#include "Table.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

// ----------- Layout
constexpr uint8_t HEADING_TEXT_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t SUBHEADING_TEXT_SIZE = 2;
constexpr uint8_t CALIBRATION_TEXT_SIZE = 2;
constexpr int16_t BAR_GAP = 1; // pixels, black separator line between adjacent bars/blocks
constexpr int16_t HEADING_MARGIN = 8; // pixels between the heading and the grid, and between the grid and the calibration table
constexpr int16_t TABLE_GAP = 16; // pixels between the editable calibration table and the read-only reference table
constexpr int16_t BAR_MARKER_OFFSET = 10; // pixels above each bar column where its single-pixel color marker is drawn
constexpr uint16_t MAX_BARS = 32; // number of bar columns; a 565 display can't represent many more distinct colors than this within a single hue/lightness band anyway (see generateHuePalette() and friends)
constexpr int16_t HUES_LABEL_GAP = 4; // pixels between the Hues view's left-side lightness value labels and the grid itself

// Default saturation applied to the Hues view's palette.
constexpr float DEFAULT_SATURATION = 0.80f;

// ----------- View Selection
enum class View : uint8_t { HuesByValue, Hues, HuesByLuminance, HuesBySaturation, Luminance, PlottingColors };
constexpr uint8_t NUM_VIEWS = 6;

struct ViewInfo
{
   const char* name;
};

constexpr ViewInfo VIEWS[NUM_VIEWS] =
{
   { "Hues by Value" },
   { "Hues by Lightness" },
   { "Hues by Luminance" },
   { "Hues by Saturation" },
   { "Luminance Sorting" },
   { "Recommended Plotting Colors" },
};

View currentView = View::HuesByValue;

// ----------- The Board
ESP32_S3_Playground arduino;

// Width of each color bar and left edge of the grid, computed once in setup() to fill as
// much of the display width as possible for exactly MAX_BARS bars (plus BAR_GAP between
// them). Used only by the Hues view - the Luminance view uses its own fixed-width, packed
// bar layout (see LUM_BAR_WIDTH) so more hue/saturation/lightness combinations fit.
int16_t barWidth = 2;
int16_t barsLeft = 0;

// Number of bars that actually fit across the display (see setup()); the Hues view's
// palettes are generated with exactly this many entries, since bars are laid out in a
// single non-wrapping row.
uint16_t numBars = 0;

// Y coordinate directly below the heading/subheading, shared starting point for both
// views' content (see enterView()).
int16_t contentTop = 0;

// fromHSL()/srgbToLinear()/toLuminance()/findLightnessForLuminance() and the
// WEIGHTING_NONE/GAMMA/HSP constants now live in Color565 (see Color.h) since they're
// general-purpose color math, not specific to this sketch. Pulled in here via `using` so
// existing call sites throughout the sketch don't need to be qualified.
using Color565::fromHSL;
using Color565::fromHSV;
using Color565::srgbToLinear;
using Color565::toLuminance;
using Color565::findLightnessForLuminance;
using Color565::WEIGHTING_NONE;
using Color565::WEIGHTING_GAMMA;
using Color565::WEIGHTING_HSP;

///
/// <summary>
/// Rounding FloatEditor step: same linear stepping as the base class, but rounds the
/// result to the nearest 0.01 so repeated adjustment can't drift away from a clean
/// multiple of the step size due to float rounding error.
/// </summary>
///
class RoundedFloatEditor : public FloatEditor
{
public:
   RoundedFloatEditor(float minValue, float maxValue, float step, float defaultValue, const char* format)
      : FloatEditor(minValue, maxValue, step, defaultValue, format)
   {}

protected:
   float _stepValue(float current, int32_t direction) override
   {
      float stepped = current + (direction * _step);
      return std::round(stepped * 100.0f) / 100.0f;
   }
};

// Default luminance coefficients: Rec. 709 (sRGB/HD).
constexpr float DEFAULT_R = 0.2126f;
constexpr float DEFAULT_G = 0.7152f;
constexpr float DEFAULT_B = 0.0722f;

RoundedFloatEditor rCoeffEditor(0.0f, 1.0f, 0.01f, DEFAULT_R, "#.####");
RoundedFloatEditor gCoeffEditor(0.0f, 1.0f, 0.01f, DEFAULT_G, "#.####");
RoundedFloatEditor bCoeffEditor(0.0f, 1.0f, 0.01f, DEFAULT_B, "#.####");

// Weighting applied on top of the R/G/B coefficients above. The coefficients answer "how
// much does each channel contribute to brightness"; weighting answers "how does that
// numeric result relate to what a human actually perceives" (plain gamma-encoded values
// aren't perceptually linear - 50% isn't half as bright as 100%). "HSP" is the only mode
// that can actually reorder colors differently than the others - see toLuminance().
const char* const WEIGHTING_LABELS[] =
{
   "None",
   "Gamma",
   "HSP",
};
// WEIGHTING_NONE/GAMMA/HSP are pulled in from Color565 via the `using` declarations above.

EnumEditor weightingEditor(WEIGHTING_LABELS, WEIGHTING_NONE, "###############");

// Default saturation/lightness levels crossed with hue to build the Luminance view's
// spread (see generateLuminancePalette()); now editable live via calibrationEditor's
// "Saturation 1"/"Saturation 2"/"Lightness 1"/"Lightness 2" rows below.
constexpr float DEFAULT_LUM_SATURATION_1 = 0.50f;
constexpr float DEFAULT_LUM_SATURATION_2 = 1.00f;
constexpr float DEFAULT_LUM_LIGHTNESS_1 = 0.35f;
constexpr float DEFAULT_LUM_LIGHTNESS_2 = 0.65f;

RoundedFloatEditor lumSaturation1Editor(0.0f, 1.0f, 0.05f, DEFAULT_LUM_SATURATION_1, "#.##");
RoundedFloatEditor lumSaturation2Editor(0.0f, 1.0f, 0.05f, DEFAULT_LUM_SATURATION_2, "#.##");
RoundedFloatEditor lumLightness1Editor(0.0f, 1.0f, 0.05f, DEFAULT_LUM_LIGHTNESS_1, "#.##");
RoundedFloatEditor lumLightness2Editor(0.0f, 1.0f, 0.05f, DEFAULT_LUM_LIGHTNESS_2, "#.##");

// ----------- Luminance view's calibration table (Saturation/Lightness levels used to
// generate the palette, then Weighting + Red/Green/Blue used to sort it). Editable live
// with the encoders; loaded/saved against PREF_NAMESPACE.
FieldTableEditor::Row calibrationRows[] =
{
   { "Saturation 1", &lumSaturation1Editor },
   { "Saturation 2", &lumSaturation2Editor },
   { "Lightness 1", &lumLightness1Editor },
   { "Lightness 2", &lumLightness2Editor },
   { "Weighting", &weightingEditor },
   { "Red", &rCoeffEditor },
   { "Green", &gCoeffEditor },
   { "Blue", &bCoeffEditor },
};

// Must be <= 15 characters: ESP32's NVS Preferences enforces a 15-character namespace
// length limit, and load()/save() silently fail to persist anything past it.
constexpr const char* PREF_NAMESPACE = "color_calib";

FieldTableEditor calibrationEditor(&arduino, PREF_NAMESPACE, calibrationRows, CALIBRATION_TEXT_SIZE);

// ----------- Luminance view's reference table: display-only, shown to the right of the
// editable calibration table, listing R/G/B coefficients for well-known luminance formulas
// so the user has something to compare their own (editable) coefficients against. Rows are
// R/G/B (unlabeled, since they line up with calibrationEditor's own Red/Green/Blue rows to
// the left) and columns are the formulas, so its layout mirrors calibrationEditor's rows.
struct ReferenceFormula
{
   const char* name;
   float r;
   float g;
   float b;
};
constexpr ReferenceFormula REFERENCE_FORMULAS[] =
{
   { "601",  0.299f,  0.587f,  0.114f },
   { "709",  0.2126f, 0.7152f, 0.0722f },
   { "2020", 0.2627f, 0.6780f, 0.0593f },
};
constexpr size_t NUM_REFERENCE_FORMULAS = sizeof(REFERENCE_FORMULAS) / sizeof(REFERENCE_FORMULAS[0]);

const Table::Column referenceColumns[] =
{
   { "", Table::Alignment::LEFT },
   { REFERENCE_FORMULAS[0].name, "#.##", Table::Alignment::RIGHT },
   { REFERENCE_FORMULAS[1].name, "#.##", Table::Alignment::RIGHT },
   { REFERENCE_FORMULAS[2].name, "#.##", Table::Alignment::RIGHT },
};

Table::Row referenceRows[] =
{
   { "" },
   { "" },
   { "" },
};

Table referenceTable(&arduino, 0, 0, referenceColumns, referenceRows, CALIBRATION_TEXT_SIZE);

// The coefficients/weighting as of the last recompute, used to detect when the table has
// changed and the Luminance view's sort needs to be rebuilt.
float lastR = DEFAULT_R;
float lastG = DEFAULT_G;
float lastB = DEFAULT_B;
long lastWeighting = WEIGHTING_NONE;

// The saturation/lightness levels as of the last palette generation, used to detect when
// the palette itself (not just the sort) needs to be rebuilt.
float lastLumSaturation1 = DEFAULT_LUM_SATURATION_1;
float lastLumSaturation2 = DEFAULT_LUM_SATURATION_2;
float lastLumLightness1 = DEFAULT_LUM_LIGHTNESS_1;
float lastLumLightness2 = DEFAULT_LUM_LIGHTNESS_2;

// Reverses the Luminance view's sort order, toggled via encoderA's button - the same
// interaction Color_Playground uses to reverse its own sort order (there, encoderB's
// button is used instead, since here it already resets the calibration table).
bool sortReversed = false;

///
/// <summary>
/// Generates a row's palette: `count` evenly-spaced hues at the given fixed saturation/
/// lightness, so only hue varies. Used by the Hues by Lightness and Hues by Saturation
/// views - every level's palette shares the same hue-to-column mapping, so the stacked
/// blocks line up in each column.
/// </summary>
/// <param name="count">Exact number of colors to generate.</param>
/// <param name="saturation">Fixed saturation applied to every entry.</param>
/// <param name="lightness">Fixed lightness applied to every entry.</param>
/// <returns>The generated palette, with exactly `count` entries.</returns>
///
std::vector<Color> generateHuePalette(size_t count, float saturation, float lightness)
{
   std::vector<Color> result;
   result.reserve(count);
   for (size_t i = 0; i < count; i++)
   {
      float hue = 360.0f * (float)i / (float)count;
      result.push_back(fromHSL(hue, saturation, lightness));
   }
   return result;
}

///
/// <summary>
/// Reusable grid used by the Hues by Value, Hues by Lightness, Hues by Luminance, and Hues
/// by Saturation views: each draws NumRows stacked color blocks per bar column (one per
/// row-editor value, top to bottom), a matching stack of single-pixel marker rows above
/// the grid, and its editable values inline - one row-editor value per bar row to the left
/// of the grid, plus a single shared-editor value on its own line below the grid. The four
/// views differ only in which HSL/HSV component each row/shared editor represents and how a
/// row's color is computed from them (colorFn), so that's the only piece each view supplies
/// itself.
/// </summary>
/// <typeparam name="NumRows">Number of stacked rows/blocks per bar column.</typeparam>
///
template<size_t NumRows>
class HueGrid
{
public:
   using ColorFunc = std::function<Color(float hue, float sharedValue, float rowValue)>;
   using ExtraChangedFunc = std::function<bool()>;

   ///
   /// <summary>
   /// Initializes a new instance of the HueGrid class.
   /// </summary>
   /// <param name="arduino">Board providing the display/encoders.</param>
   /// <param name="prefNamespace">Preferences namespace used to persist field values.</param>
   /// <param name="sharedLabel">Label for the shared editor, e.g. "Saturation" or "Lightness".</param>
   /// <param name="sharedEditor">Editor applied identically to every row/column.</param>
   /// <param name="rowLabels">Per-row labels, e.g. "Lightness 1".."Lightness N", used for Preferences key derivation.</param>
   /// <param name="rowEditors">Per-row editors, one per stacked block.</param>
   /// <param name="colorFn">Computes a bar's color from its hue, the shared editor's value, and this row's editor value.</param>
   /// <param name="extraChangedFn">Optional check for staleness caused by values outside this grid's own fields (e.g. shared Weighting/R/G/B coefficients).</param>
   ///
   HueGrid(Arduino* arduino, const char* prefNamespace, const char* sharedLabel, RoundedFloatEditor* sharedEditor,
      const char* const (&rowLabels)[NumRows], RoundedFloatEditor (&rowEditors)[NumRows], ColorFunc colorFn, ExtraChangedFunc extraChangedFn = nullptr)
      : _arduino(arduino), _sharedLabel(sharedLabel), _sharedEditor(sharedEditor), _rowEditors(rowEditors), _colorFn(colorFn), _extraChangedFn(extraChangedFn)
   {
      _fields[0] = FieldEditor::FieldInfo(sharedLabel, sharedEditor);
      for (size_t i = 0; i < NumRows; i++)
      {
         _fields[i + 1] = FieldEditor::FieldInfo(rowLabels[i], &rowEditors[i]);
      }
      _editor = new FieldEditor(arduino, prefNamespace, _fields);

      _lastShared = sharedEditor->get();
      for (size_t i = 0; i < NumRows; i++)
      {
         _lastRow[i] = rowEditors[i].get();
      }
   }

   ///
   /// <summary>
   /// Loads the grid's field values from Preferences.
   /// </summary>
   ///
   void load()
   {
      _editor->load();
   }

   ///
   /// <summary>
   /// Generates every row's palette, sized to exactly `count` entries, at the live shared/
   /// row values, so they all share the same hue-to-column mapping.
   /// </summary>
   /// <param name="count">Exact number of colors to generate per palette.</param>
   ///
   void generatePalettes(size_t count)
   {
      _lastShared = _sharedEditor->get();
      for (size_t rIndex = 0; rIndex < NumRows; rIndex++)
      {
         _lastRow[rIndex] = _rowEditors[rIndex].get();

         std::vector<Color> palette;
         palette.reserve(count);
         for (size_t i = 0; i < count; i++)
         {
            float hue = 360.0f * (float)i / (float)count;
            palette.push_back(_colorFn(hue, _lastShared, _lastRow[rIndex]));
         }
         _palettes[rIndex] = std::move(palette);
      }
   }

   ///
   /// <summary>
   /// Draws every bar column's grid as a stack of NumRows blocks (one per row, top to
   /// bottom), separated by BAR_GAP, plus a matching stack of marker rows BAR_MARKER_OFFSET
   /// pixels above the grid (one row per level, same order/spacing as the blocks below).
   /// Each marker row is left black except for a single pixel centered within that row, in
   /// that level's color.
   /// </summary>
   ///
   void drawGrid()
   {
      constexpr int16_t dotSize = 1;
      int16_t markerHeight = (int16_t)NumRows * (_blockHeight + BAR_GAP) - BAR_GAP;
      int16_t markerTop = _barsTop - BAR_MARKER_OFFSET - markerHeight;

      for (uint16_t index = 0; index < numBars; index++)
      {
         int16_t x = barsLeft + index * (barWidth + BAR_GAP);
         int16_t dotX = x + (barWidth - dotSize) / 2;

         _arduino->fillRect(x, markerTop, barWidth, markerHeight, Color::BLACK);

         int16_t markerY = markerTop;
         int16_t barY2 = _barsTop;
         for (size_t rIndex = 0; rIndex < NumRows; rIndex++)
         {
            Color color = _palettes[rIndex][index];
            _arduino->fillRect(dotX, markerY + (_blockHeight - dotSize) / 2, dotSize, dotSize, color);
            _arduino->fillRect(x, barY2, barWidth, _blockHeight, color);
            markerY += _blockHeight + BAR_GAP;
            barY2 += _blockHeight + BAR_GAP;
         }
      }
   }

   ///
   /// <summary>
   /// Draws the grid's editable values inline, next to the content each one controls: each
   /// row value is drawn to the left of the grid, vertically centered on its own bar row,
   /// and the shared value is drawn as its own line below the grid. Reads selection state
   /// from the internal FieldEditor to highlight whichever field is currently selected.
   /// </summary>
   ///
   void drawFields()
   {
      uint8_t selected = _editor->selectedIndex();

      _arduino->setTextSize(CALIBRATION_TEXT_SIZE);

      // Only repaints a field's rect/text when its text or color actually changed since
      // the last call (or it hasn't been drawn yet), avoiding the flicker unconditional
      // fillRect()+print() on every field would cause whenever just the selection moves.
      auto drawField = [&](uint8_t fieldIndex, int16_t x, int16_t y, int16_t w, int16_t h, const std::string& text, Color valueColor, Color backgroundColor)
      {
         if (_fieldDrawn[fieldIndex] && text == _lastFieldText[fieldIndex] &&
            valueColor == _lastFieldColor[fieldIndex] && backgroundColor == _lastFieldBackgroundColor[fieldIndex])
         {
            return;
         }

         _arduino->fillRect(x, y, w, h, Color::BLACK);
         _arduino->setCursor(x, y);
         _arduino->print(text.c_str(), valueColor, backgroundColor);

         _lastFieldText[fieldIndex] = text;
         _lastFieldColor[fieldIndex] = valueColor;
         _lastFieldBackgroundColor[fieldIndex] = backgroundColor;
         _fieldDrawn[fieldIndex] = true;
      };

      // Row values, one per row, to the left of the grid, vertically centered on that row
      // (rows 1..NumRows in _fields; row 0 is the shared value).
      int16_t barY = _barsTop;
      for (size_t rIndex = 0; rIndex < NumRows; rIndex++)
      {
         ValueBase* value = _fields[rIndex + 1].value;
         Color valueColor;
         Color backgroundColor;
         _editor->colorsFor(static_cast<uint8_t>(rIndex + 1), valueColor, backgroundColor);

         drawField(static_cast<uint8_t>(rIndex + 1), 0, barY + (_blockHeight - _arduino->charH()) / 2,
            _labelWidth - HUES_LABEL_GAP, _blockHeight, value->valueText(), valueColor, backgroundColor);
         barY += _blockHeight + BAR_GAP;
      }

      // Shared value, drawn as its own "<Label> X.XX" line below the grid.
      {
         ValueBase* value = _fields[0].value;
         Color valueColor;
         Color backgroundColor;
         _editor->colorsFor(0, valueColor, backgroundColor);

         if (!_fieldDrawn[0])
         {
            _arduino->setCursor(0, _sharedValueY);
            _arduino->print(_sharedLabelText.c_str(), Color::LABEL);
         }

         drawField(0, _sharedValueX, _sharedValueY, (int16_t)_arduino->width() - _sharedValueX,
            _arduino->charH(), value->valueText(), valueColor, backgroundColor);
      }

      _lastSelectedIndex = selected;
   }

   ///
   /// <summary>
   /// Lays out and draws the grid: computes the label width, bar geometry, block/bar
   /// heights to fill the space between the heading/markers and the shared-value line
   /// (positioned directly below the grid so it never overlaps the bars), generates the
   /// palettes, then draws the grid and fields. Call whenever the view becomes active.
   /// </summary>
   ///
   void enter()
   {
      // Force every field to be fully redrawn: the caller just cleared the display, so the
      // "skip unchanged fields" tracking in drawFields() (_fieldDrawn[]) would otherwise
      // think each field's text/color is unchanged from before the clear and skip
      // redrawing it, leaving the fields blank.
      std::fill(std::begin(_fieldDrawn), std::end(_fieldDrawn), false);

      generatePalettes(numBars);

      // Reserve space on the left for each row's value ("0.00" is the widest possible
      // value), then recompute barWidth/barsLeft (originally sized in setup() for the full
      // display width) to fill only the remaining width to its right, so the grid no
      // longer overlaps the values.
      _arduino->setTextSize(CALIBRATION_TEXT_SIZE);
      _labelWidth = _arduino->textWidth("0.00") + HUES_LABEL_GAP;

      int16_t availableWidth = (int16_t)_arduino->width() - _labelWidth;
      barWidth = std::max((int16_t)1, (int16_t)((availableWidth + BAR_GAP) / (int16_t)numBars - BAR_GAP));
      int16_t barsWidth = numBars * (barWidth + BAR_GAP) - BAR_GAP;
      barsLeft = _labelWidth + (int16_t)((availableWidth - barsWidth) / 2);

      // Compute the block height that fills whatever vertical space is left over once the
      // content top, the marker dot rows (BAR_MARKER_OFFSET + one dot-height stack, same
      // height as the grid itself), the grid, and the shared-value line have all been
      // accounted for.
      int16_t fixedHeight = BAR_MARKER_OFFSET + HEADING_MARGIN + HEADING_MARGIN + _arduino->charH();
      int16_t availableHeight = (int16_t)_arduino->height() - contentTop - fixedHeight;
      _blockHeight = std::max((int16_t)((availableHeight + BAR_GAP) / (2 * (int16_t)NumRows) - BAR_GAP), (int16_t)1);
      _barHeight = (int16_t)NumRows * (_blockHeight + BAR_GAP) - BAR_GAP;

      _barsTop = contentTop + BAR_MARKER_OFFSET + _barHeight;

      // Always position the shared-value line directly below the actual bottom of the
      // grid, rather than relying on the (potentially inexact, due to integer rounding
      // above) space-filling math to land exactly there - this guarantees it never
      // overlaps the bars.
      _sharedValueY = _barsTop + _barHeight + HEADING_MARGIN;
      _sharedLabelText = std::string(_sharedLabel) + " ";
      _sharedValueX = _arduino->textWidth(_sharedLabelText.c_str());

      drawGrid();
      drawFields();
   }

   ///
   /// <summary>
   /// Drives the grid from the board's own encoders in a single call, mirroring
   /// FieldEditor::loop(): selects/adjusts/persists fields, and regenerates + redraws the
   /// grid whenever the shared/row values (or, if extraChangedFn was supplied, any
   /// dependency outside this grid's own fields) have changed since the last call.
   /// </summary>
   ///
   void loop()
   {
      bool fieldsChanged = _editor->loop();

      bool changed = _sharedEditor->get() != _lastShared || (_extraChangedFn && _extraChangedFn());
      for (size_t rIndex = 0; rIndex < NumRows; rIndex++)
      {
         if (_rowEditors[rIndex].get() != _lastRow[rIndex])
         {
            changed = true;
         }
      }

      if (changed)
      {
         generatePalettes(numBars);
         drawGrid();
         fieldsChanged = true;
      }

      if (fieldsChanged)
      {
         drawFields();
      }
   }

private:
   Arduino* _arduino;
   const char* _sharedLabel;
   RoundedFloatEditor* _sharedEditor;
   RoundedFloatEditor* _rowEditors;
   ColorFunc _colorFn;
   ExtraChangedFunc _extraChangedFn;

   FieldEditor::FieldInfo _fields[NumRows + 1];
   FieldEditor* _editor;

   std::vector<Color> _palettes[NumRows];

   float _lastShared = 0.0f;
   float _lastRow[NumRows] = {};

   int16_t _labelWidth = 0;
   int16_t _barsTop = 0;
   int16_t _blockHeight = 1;
   int16_t _barHeight = 1;
   int16_t _sharedValueY = 0;
   int16_t _sharedValueX = 0;
   std::string _sharedLabelText;

   uint8_t _lastSelectedIndex = 0;
   std::string _lastFieldText[NumRows + 1];
   Color _lastFieldColor[NumRows + 1] = {};
   Color _lastFieldBackgroundColor[NumRows + 1] = {};
   bool _fieldDrawn[NumRows + 1] = {};
};

// ----------- Hues by Value view: same layout idea as the Hues view below, but using HSV
// instead of HSL - each row varies Value (brightness) at the shared Saturation value,
// using fromHSV() instead of fromHSL() for its per-column color formula.

// Default value (brightness) levels shown as the stacked blocks within each bar column,
// top to bottom. 100% is omitted since at full saturation it's just the pure hue color
// regardless - the interesting variation is at lower values. Editable live via
// huesByValueGrid's "Value 1".."Value N" rows (see huesByValueEditors below); this array
// only supplies their default values.
constexpr float DEFAULT_VALUE_LEVELS[] = { 0.20f, 0.40f, 0.60f, 0.80f };
constexpr size_t NUM_VALUE_LEVELS = sizeof(DEFAULT_VALUE_LEVELS) / sizeof(DEFAULT_VALUE_LEVELS[0]);

// Saturation applied to every entry in every huesByValueGrid row, editable live via
// huesByValueGrid (Encoder B).
RoundedFloatEditor huesByValueSaturationEditor(0.0f, 1.0f, 0.05f, DEFAULT_SATURATION, "#.##");

// One editable value per stacked row, defaulted from DEFAULT_VALUE_LEVELS, each exposed as
// its own "Value N" row in huesByValueGrid below.
RoundedFloatEditor huesByValueEditors[NUM_VALUE_LEVELS] =
{
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_VALUE_LEVELS[0], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_VALUE_LEVELS[1], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_VALUE_LEVELS[2], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_VALUE_LEVELS[3], "#.##"),
};

// Labels for each huesByValueEditors row, e.g. "Value 1", "Value 2", etc. Used only for
// Preferences key derivation - the grid draws each row's live value directly without its
// own label, since row position already identifies which value level it is.
const char* const HUES_VALUE_LABELS[] = { "Value 1", "Value 2", "Value 3", "Value 4" };

// The Hues by Value view's grid: rows vary Value at the shared Saturation value, so each
// bar's color is a straightforward fromHSV() at that row's value.
HueGrid<NUM_VALUE_LEVELS> huesByValueGrid(&arduino, PREF_NAMESPACE, "Saturation", &huesByValueSaturationEditor,
   HUES_VALUE_LABELS, huesByValueEditors,
   [](float hue, float saturation, float value) { return fromHSV(hue, saturation, value); });

// ----------- Hues view: a single set of NUM_LIGHTNESS_LEVELS generated color palettes,
// one per lightness level at the saturation set by this view's one-row calibration table,
// all sharing the same hue-to-column mapping and left in ascending-hue order (no
// sorting/weighting). Hues by Saturation (below) mirrors this with Saturation/Lightness
// swapped, and Hues by Luminance (below that) mirrors it again with the per-row values
// reinterpreted as target luminance. All three share their grid/field layout and
// draw/change-tracking logic via HueGrid; only their per-column color formula differs.

// Default lightness levels shown as the stacked blocks within each bar column, top to
// bottom. 100% is omitted since it's just white regardless of hue/saturation. Ordered
// ascending so the smallest (darkest/lowest-luminance) level is on top. These are now
// editable live via huesGrid's "Lightness 1".."Lightness N" rows (see huesLightnessEditors
// below); this array only supplies their default values.
constexpr float DEFAULT_LIGHTNESS_LEVELS[] = { 0.20f, 0.40f, 0.60f, 0.80f };
constexpr size_t NUM_LIGHTNESS_LEVELS = sizeof(DEFAULT_LIGHTNESS_LEVELS) / sizeof(DEFAULT_LIGHTNESS_LEVELS[0]);

// Saturation (chroma) applied to every entry in every huesGrid row, editable live via
// huesGrid (Encoder B).
RoundedFloatEditor saturationEditor(0.0f, 1.0f, 0.05f, DEFAULT_SATURATION, "#.##");

// One editable lightness value per stacked row, defaulted from DEFAULT_LIGHTNESS_LEVELS,
// each exposed as its own "Lightness N" row in huesGrid below.
RoundedFloatEditor huesLightnessEditors[NUM_LIGHTNESS_LEVELS] =
{
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LIGHTNESS_LEVELS[0], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LIGHTNESS_LEVELS[1], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LIGHTNESS_LEVELS[2], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LIGHTNESS_LEVELS[3], "#.##"),
};

// Labels for each huesLightnessEditors row, e.g. "Lightness 1", "Lightness 2", etc. Used
// only for Preferences key derivation - the grid draws each row's live value directly
// without its own label, since row position already identifies which lightness level it is.
const char* const HUES_LIGHTNESS_LABELS[] = { "Lightness 1", "Lightness 2", "Lightness 3", "Lightness 4" };

// The Hues view's grid: rows vary Lightness at the shared Saturation value, so each bar's
// color is a straightforward fromHSL() at that row's lightness.
HueGrid<NUM_LIGHTNESS_LEVELS> huesGrid(&arduino, PREF_NAMESPACE, "Saturation", &saturationEditor,
   HUES_LIGHTNESS_LABELS, huesLightnessEditors,
   [](float hue, float saturation, float lightness) { return fromHSL(hue, saturation, lightness); });

// ----------- Hues by Saturation view: same layout idea as the Hues view above, but with
// Saturation and Lightness swapped - one editable Saturation value per stacked row, and a
// single shared Lightness field applied to every row/column.

// Default lightness applied to every entry in every huesSatGrid row. Deliberately not
// reusing DEFAULT_SATURATION (0.80f) here - that's a reasonable chroma default, but as a
// single shared lightness it would wash out most hues toward white.
constexpr float DEFAULT_HUES_SAT_LIGHTNESS = 0.50f;

// Lightness (shared across every row/column), editable live via huesSatGrid (Encoder B).
RoundedFloatEditor huesSatLightnessEditor(0.0f, 1.0f, 0.05f, DEFAULT_HUES_SAT_LIGHTNESS, "#.##");

// One editable saturation value per stacked row, defaulted from DEFAULT_LIGHTNESS_LEVELS
// (reusing the same 0.20/0.40/0.60/0.80 spread), each exposed as its own "Saturation N" row
// in huesSatGrid below.
RoundedFloatEditor huesSatSaturationEditors[NUM_LIGHTNESS_LEVELS] =
{
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LIGHTNESS_LEVELS[0], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LIGHTNESS_LEVELS[1], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LIGHTNESS_LEVELS[2], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LIGHTNESS_LEVELS[3], "#.##"),
};

// Labels for each huesSatSaturationEditors row, e.g. "Saturation 1", "Saturation 2", etc.
// Used only for Preferences key derivation.
const char* const HUES_SAT_SATURATION_LABELS[] = { "Saturation 1", "Saturation 2", "Saturation 3", "Saturation 4" };

// The Hues by Saturation view's grid: rows vary Saturation at the shared Lightness value,
// so the shared/row values must be swapped back into fromHSL()'s (saturation, lightness)
// order.
HueGrid<NUM_LIGHTNESS_LEVELS> huesSatGrid(&arduino, PREF_NAMESPACE, "Lightness", &huesSatLightnessEditor,
   HUES_SAT_SATURATION_LABELS, huesSatSaturationEditors,
   [](float hue, float lightness, float saturation) { return fromHSL(hue, saturation, lightness); });

// ----------- Hues by Luminance view: mirrors the Hues view above, but instead of using
// each row's value directly as the HSL lightness, treats it as a target perceived
// luminance (see toLuminance()) and solves for the lightness that achieves it at each hue
// (see findLightnessForLuminance()), using the shared Weighting/Red/Green/Blue
// coefficients from the Luminance view's calibration table - so every bar in a given row
// reads as equally bright regardless of hue, rather than sharing a raw lightness value
// that different hues perceive differently.

// Default target luminance levels shown as the stacked blocks within each bar column, top
// to bottom - reuses the same 0.20/0.40/0.60/0.80 spread as DEFAULT_LIGHTNESS_LEVELS, just
// interpreted as a target perceived luminance instead of a raw HSL lightness. These are
// editable live via huesLumGrid's "Luminance 1".."Luminance N" rows (see
// huesLumLevelEditors below); this array only supplies their default values.
constexpr float DEFAULT_LUMINANCE_LEVELS[] = { 0.20f, 0.40f, 0.60f, 0.80f };
constexpr size_t NUM_LUMINANCE_LEVELS = sizeof(DEFAULT_LUMINANCE_LEVELS) / sizeof(DEFAULT_LUMINANCE_LEVELS[0]);

// Saturation (chroma) applied to every entry in every huesLumGrid row, editable live via
// huesLumGrid (Encoder B).
RoundedFloatEditor huesLumSaturationEditor(0.0f, 1.0f, 0.05f, DEFAULT_SATURATION, "#.##");

// One editable target-luminance value per stacked row, defaulted from
// DEFAULT_LUMINANCE_LEVELS, each exposed as its own "Luminance N" row in huesLumGrid below.
RoundedFloatEditor huesLumLevelEditors[NUM_LUMINANCE_LEVELS] =
{
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LUMINANCE_LEVELS[0], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LUMINANCE_LEVELS[1], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LUMINANCE_LEVELS[2], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LUMINANCE_LEVELS[3], "#.##"),
};

// Labels for each huesLumLevelEditors row, e.g. "Luminance 1", "Luminance 2", etc. Used
// only for Preferences key derivation.
const char* const HUES_LUM_LABELS[] = { "Luminance 1", "Luminance 2", "Luminance 3", "Luminance 4" };

// The Hues by Luminance view's grid: for each column's hue, solves for the lightness that
// achieves that row's target luminance using the shared Weighting/Red/Green/Blue
// coefficients from the Luminance view's calibration table (read live from those globals,
// so extraChangedFn below reports staleness whenever any of them change even though
// they're not this grid's own fields).
HueGrid<NUM_LUMINANCE_LEVELS> huesLumGrid(&arduino, PREF_NAMESPACE, "Saturation", &huesLumSaturationEditor,
   HUES_LUM_LABELS, huesLumLevelEditors,
   [](float hue, float saturation, float targetLuminance)
   {
      float lightness = findLightnessForLuminance(hue, saturation, rCoeffEditor.get(), gCoeffEditor.get(), bCoeffEditor.get(),
         weightingEditor.get(), targetLuminance);
      return fromHSL(hue, saturation, lightness);
   },
   []()
   {
      static long lastWeightingCheck = weightingEditor.get();
      static float lastRCheck = rCoeffEditor.get();
      static float lastGCheck = gCoeffEditor.get();
      static float lastBCheck = bCoeffEditor.get();

      bool changed = weightingEditor.get() != lastWeightingCheck || rCoeffEditor.get() != lastRCheck ||
         gCoeffEditor.get() != lastGCheck || bCoeffEditor.get() != lastBCheck;

      lastWeightingCheck = weightingEditor.get();
      lastRCheck = rCoeffEditor.get();
      lastGCheck = gCoeffEditor.get();
      lastBCheck = bCoeffEditor.get();
      return changed;
   });

// ----------- Luminance view: a single flat palette generated from a spread of hue,
// saturation, and lightness combinations (see generateLuminancePalette()), sorted by the
// calibration table's luminance formula (lightest on the left) and drawn as a single row
// of bars, with a matching row of marker dots above (same idea as the Hues view's marker
// rows, just a single row here since there's only one row of bars).

// Number of saturation/lightness levels crossed with hue to build the Luminance view's
// spread (see generateLuminancePalette()); the actual values are editable via
// calibrationEditor's "Saturation 1"/"Saturation 2"/"Lightness 1"/"Lightness 2" rows.
// Assumes luminanceNumBars divides evenly by NUM_LUM_SATURATION_LEVELS *
// NUM_LUM_LIGHTNESS_LEVELS so every combination gets the same number of hues; documented
// here rather than handled generically since luminanceNumBars is computed once from a
// fixed bar width (see LUM_BAR_WIDTH).
constexpr size_t NUM_LUM_SATURATION_LEVELS = 2;
constexpr size_t NUM_LUM_LIGHTNESS_LEVELS = 2;

// Width of each Luminance-view bar, deliberately much narrower than the Hues view's
// barWidth so more hue/saturation/lightness combinations fit across the display.
constexpr int16_t LUM_BAR_WIDTH = 5;

// Number of bars and left edge of the Luminance view's row, computed once in setup() to
// pack as many LUM_BAR_WIDTH-wide bars as possible across the display width.
uint16_t luminanceNumBars = 0;
int16_t luminanceBarsLeft = 0;

// The generated palette (see generateLuminancePalette()), sized to fill exactly
// luminanceNumBars bar positions.
std::vector<Color> luminancePalette;

// Cached perceived brightness for each luminancePalette entry; reused across a recompute
// (only the resulting luminanceSortedIndices needs to be kept afterward).
std::vector<float> luminanceValues;

// Maps a bar position to the palette index that should be displayed there, sorted
// descending by the calibration table's luminance formula so the lightest bar is on the
// left (or ascending, if sortReversed). Rebuilt by recomputeLuminance().
std::vector<size_t> luminanceSortedIndices;

// Top Y coordinate and height of the Luminance view's single row of bars, computed in
// enterLuminance() to fill the vertical space left over once the heading, marker row, and
// calibration table have been accounted for.
int16_t luminanceBarsTop = 0;
int16_t luminanceBarHeight = 1;

///
/// <summary>
/// Generates the Luminance view's palette: the first entries are every combination of
/// primary/half-primary RGB values (0, 128, 255 per channel), excluding pure black and pure
/// white, since those aren't useful for comparing perceived brightness across hues. Any
/// remaining bar positions are filled with every combination of LUM_SATURATION_LEVELS and
/// LUM_LIGHTNESS_LEVELS, each crossed with an even spread of hues, the same as before (see
/// the assumption documented on LUM_SATURATION_LEVELS above). Final on-screen order comes
/// from sorting by luminance (see recomputeLuminance()), so the order colors are generated
/// in here doesn't matter.
/// </summary>
/// <param name="count">Exact number of colors to generate.</param>
/// <returns>The generated palette, with exactly `count` entries.</returns>
///
std::vector<Color> generateLuminancePalette(size_t count)
{
   std::vector<Color> result;
   result.reserve(count);

   // Primaries and half-primaries: every combination of 0/128/255 per channel, excluding
   // pure black (0,0,0) and pure white (255,255,255).
   constexpr uint8_t PRIMARY_LEVELS[] = { 0, 128, 255 };
   for (uint8_t r : PRIMARY_LEVELS)
   {
      for (uint8_t g : PRIMARY_LEVELS)
      {
         for (uint8_t b : PRIMARY_LEVELS)
         {
            if (result.size() >= count)
            {
               return result;
            }
            if ((r == 0 && g == 0 && b == 0) || (r == 255 && g == 255 && b == 255))
            {
               continue;
            }
            result.push_back(Color565::fromRGB(r, g, b));
         }
      }
   }

   size_t remaining = count - result.size();
   if (remaining == 0)
   {
      return result;
   }

   float saturationLevels[NUM_LUM_SATURATION_LEVELS] = { lumSaturation1Editor.get(), lumSaturation2Editor.get() };
   float lightnessLevels[NUM_LUM_LIGHTNESS_LEVELS] = { lumLightness1Editor.get(), lumLightness2Editor.get() };

   size_t combos = NUM_LUM_SATURATION_LEVELS * NUM_LUM_LIGHTNESS_LEVELS;
   size_t huesPerCombo = remaining / combos;
   size_t extra = remaining % combos;

   // huesPerCombo may not divide `remaining` evenly (despite the assumption documented
   // above, which only guarantees luminanceNumBars itself divides evenly - not necessarily
   // `remaining` after the primaries/half-primaries are subtracted out). Any leftover is
   // spread one-per-combo across the first `extra` combos so exactly `remaining` entries
   // are generated, keeping the palette's total size matching `count` (callers index it
   // assuming exactly `count` entries).
   size_t comboIndex = 0;
   for (size_t sIndex = 0; sIndex < NUM_LUM_SATURATION_LEVELS; sIndex++)
   {
      for (size_t lIndex = 0; lIndex < NUM_LUM_LIGHTNESS_LEVELS; lIndex++, comboIndex++)
      {
         size_t huesThisCombo = huesPerCombo + (comboIndex < extra ? 1 : 0);
         for (size_t hIndex = 0; hIndex < huesThisCombo; hIndex++)
         {
            float hue = 360.0f * (float)hIndex / (float)huesThisCombo;
            result.push_back(fromHSL(hue, saturationLevels[sIndex], lightnessLevels[lIndex]));
         }
      }
   }

   return result;
}

///
/// <summary>
/// Rebuilds luminanceSortedIndices using the calibration table's current coefficients/
/// weighting, descending by luminance so the lightest bar ends up on the left (or
/// ascending, if sortReversed).
/// </summary>
///
void recomputeLuminance()
{
   float rWeight = rCoeffEditor.get();
   float gWeight = gCoeffEditor.get();
   float bWeight = bCoeffEditor.get();
   long weighting = weightingEditor.get();

   lastR = rWeight;
   lastG = gWeight;
   lastB = bWeight;
   lastWeighting = weighting;

   size_t count = luminancePalette.size();
   luminanceSortedIndices.resize(count);
   for (size_t i = 0; i < count; i++)
   {
      luminanceValues[i] = toLuminance(luminancePalette[i], rWeight, gWeight, bWeight, weighting);
      luminanceSortedIndices[i] = i;
   }

   std::stable_sort(luminanceSortedIndices.begin(), luminanceSortedIndices.end(), [](size_t a, size_t b)
   {
      return luminanceValues[a] > luminanceValues[b];
   });

   if (sortReversed)
   {
      std::reverse(luminanceSortedIndices.begin(), luminanceSortedIndices.end());
   }
}

///
/// <summary>
/// Draws the Luminance view's single row of bars, in luminanceSortedIndices order, plus a
/// matching row of marker dots BAR_MARKER_OFFSET pixels above - same idea as the Hues
/// view's marker rows (a single colored pixel, horizontally centered in the column,
/// against an otherwise black background), just one row since there's only one row of
/// bars here.
/// </summary>
///
void drawLuminanceRow()
{
   constexpr int16_t dotSize = 1;
   int16_t markerTop = luminanceBarsTop - BAR_MARKER_OFFSET - dotSize;

   for (uint16_t index = 0; index < luminanceNumBars; index++)
   {
      int16_t x = luminanceBarsLeft + index * (LUM_BAR_WIDTH + BAR_GAP);
      Color color = luminancePalette[luminanceSortedIndices[index]];

      arduino.fillRect(x, markerTop, LUM_BAR_WIDTH, dotSize, Color::BLACK);
      int16_t dotX = x + (LUM_BAR_WIDTH - dotSize) / 2;
      arduino.fillRect(dotX, markerTop, dotSize, dotSize, color);

      arduino.fillRect(x, luminanceBarsTop, LUM_BAR_WIDTH, luminanceBarHeight, color);
   }
}

///
/// <summary>
/// Lays out and draws the Luminance view: computes luminanceNumBars/luminanceBarsLeft to
/// pack as many LUM_BAR_WIDTH-wide bars as possible across the display, computes
/// luminanceBarsTop/luminanceBarHeight to fill the space between the heading/marker row
/// and the calibration table, positions both tables, generates and sorts the palette, then
/// draws everything.
/// </summary>
///
void enterLuminance()
{
   int16_t availableWidth = (int16_t)arduino.width();
   luminanceNumBars = (uint16_t)((availableWidth + BAR_GAP) / (LUM_BAR_WIDTH + BAR_GAP));
   int16_t luminanceBarsWidth = luminanceNumBars * (LUM_BAR_WIDTH + BAR_GAP) - BAR_GAP;
   luminanceBarsLeft = (int16_t)((arduino.width() - luminanceBarsWidth) / 2);

   luminancePalette = generateLuminancePalette(luminanceNumBars);
   luminanceValues.resize(luminancePalette.size());
   recomputeLuminance();

   lastLumSaturation1 = lumSaturation1Editor.get();
   lastLumSaturation2 = lumSaturation2Editor.get();
   lastLumLightness1 = lumLightness1Editor.get();
   lastLumLightness2 = lumLightness2Editor.get();

   int16_t fixedHeight = BAR_MARKER_OFFSET + HEADING_MARGIN + calibrationEditor.height();
   luminanceBarHeight = std::max((int16_t)(arduino.height() - contentTop - fixedHeight), (int16_t)1);
   luminanceBarsTop = contentTop + BAR_MARKER_OFFSET;

   int16_t calibrationTop = luminanceBarsTop + luminanceBarHeight + HEADING_MARGIN;
   calibrationEditor.setPosition(0, calibrationTop);

   // Bottom-align with calibrationEditor rather than top-align: calibrationEditor now has
   // extra rows (Saturation 1/2, Lightness 1/2, Weighting) above its Red/Green/Blue rows
   // that referenceTable doesn't have, so top-aligning the two tables would no longer line
   // up their R/G/B rows. Both tables end with exactly 3 (same-height) rows - Red/Green/Blue
   // here, R/G/B there - so bottom-aligning them lines those rows up instead.
   referenceTable.setPosition(calibrationEditor.width() + TABLE_GAP, calibrationTop + calibrationEditor.height(), Anchor::BOTTOM_LEFT);

   drawLuminanceRow();
   calibrationEditor.draw();
   referenceTable.draw();
}

// ----------- Plotting Colors view: two side-by-side sets of NUM_PLOT_COLORS_PER_SIDE
// colors (left and right), each set sharing the same NUM_PLOT_COLORS_PER_SIDE evenly
// spread hues but generated from its own editable target luminance and chroma
// (saturation), so the two sets can be compared side by side - e.g. to see how a change
// in either setting affects the recommended palette. Each color's lightness is solved
// (via binary search) so it hits its set's target perceived luminance under gamma
// weighting and the current Red/Green/Blue coefficients (see toLuminance()/
// WEIGHTING_GAMMA), the same approach used by the Hues by Luminance view. Drawn the same
// way as the Luminance view's single row (bars plus a matching row of marker dots above),
// just with much narrower (1 pixel wide), widely-spaced (10 pixel gap) bars since there
// are only NUM_PLOT_COLORS_PER_SIDE of them per set. Below the two sets is a pseudo table
// (label column on the left, then one value column per set) with each set's Luminance and
// Chroma editable live with the encoders (same interaction pattern as the Hues view's
// inline fields).

// Number of colors in each of the Plotting Colors view's two sets, and the fixed bar
// geometry used to draw them (deliberately different from BAR_GAP/LUM_BAR_WIDTH - this
// view isn't about packing as many bars as possible, it's about clearly separating a
// small, fixed set of recommended plotting colors). The two generated sets now share the
// primary/secondary group's hues (see PLOT_PRIMARY_SECONDARY_HUES below), so their count
// must match NUM_PLOT_PRIMARY_SECONDARY_COLORS - defined after that array, below.
constexpr int16_t PLOT_BAR_WIDTH = 1;
constexpr int16_t PLOT_BAR_GAP = 10;

// Extra horizontal gap between sets, in addition to PLOT_BAR_GAP.
constexpr int16_t PLOT_GROUP_GAP = 24;

// The fixed primary/secondary colors shown as a third group, to the far right of the two
// generated sets - unlike those sets, these are constant reference colors, not solved for
// a target luminance/chroma. Every combination of primary/half-primary levels (0/128/255
// per channel) whose R+G+B sum is >= 510 (255+255), excluding pure white, plus the three
// main primaries (full red/green/blue) even though their sum only reaches 255.
const std::vector<Color> PLOT_PRIMARY_SECONDARY_COLORS =
{
   Color565::fromRGB(255, 0, 0),
   Color565::fromRGB(0, 255, 0),
   Color565::fromRGB(0, 0, 255),
   Color565::fromRGB(0, 255, 255),
   Color565::fromRGB(128, 128, 255),
   Color565::fromRGB(128, 255, 128),
   Color565::fromRGB(128, 255, 255),
   Color565::fromRGB(255, 0, 255),
   Color565::fromRGB(255, 128, 128),
   Color565::fromRGB(255, 128, 255),
   Color565::fromRGB(255, 255, 0),
   Color565::fromRGB(255, 255, 128),
};
const size_t NUM_PLOT_PRIMARY_SECONDARY_COLORS = PLOT_PRIMARY_SECONDARY_COLORS.size();

// Number of colors in each of the Plotting Colors view's two generated sets - matches
// NUM_PLOT_PRIMARY_SECONDARY_COLORS since each generated entry shares that group's hue at
// the same index (see PLOT_PRIMARY_SECONDARY_HUES below).
constexpr size_t NUM_PLOT_COLORS_PER_SIDE = 12;

// The hue (in degrees) of each entry in PLOT_PRIMARY_SECONDARY_COLORS, in the same order,
// used by generatePlotColorsSet() so the two generated sets share the primary/secondary
// group's hues instead of an even 0-360 spread - i.e. all three groups line up by hue,
// with only luminance/chroma differing between them.
constexpr float PLOT_PRIMARY_SECONDARY_HUES[] =
{
   0.0f, 120.0f, 240.0f, 180.0f, 240.0f, 120.0f, 180.0f, 300.0f, 0.0f, 300.0f, 60.0f, 60.0f,
};
static_assert(std::size(PLOT_PRIMARY_SECONDARY_HUES) == NUM_PLOT_COLORS_PER_SIDE, "PLOT_PRIMARY_SECONDARY_HUES must have one entry per generated set color");

// Vertical gap between each bar's marker dot and its bar, and (to keep spacing visually
// even) also used between the subheading and the marker dots themselves - tripled from
// BAR_MARKER_OFFSET (used by the other views) to give the Plotting Colors view's points
// more breathing room.
constexpr int16_t PLOT_MARKER_OFFSET = BAR_MARKER_OFFSET * 3;

// Default target luminance/chroma (saturation) for the left and right sets, editable live
// via plotColorsEditor. Deliberately different defaults so the two sets look distinct out
// of the box.
constexpr float DEFAULT_PLOT_LUMINANCE_LEFT = 0.40f;
constexpr float DEFAULT_PLOT_CHROMA_LEFT = 1.00f;
constexpr float DEFAULT_PLOT_LUMINANCE_RIGHT = 0.70f;
constexpr float DEFAULT_PLOT_CHROMA_RIGHT = 0.60f;

RoundedFloatEditor plotLuminanceLeftEditor(0.0f, 1.0f, 0.05f, DEFAULT_PLOT_LUMINANCE_LEFT, "#.##");
RoundedFloatEditor plotChromaLeftEditor(0.0f, 1.0f, 0.05f, DEFAULT_PLOT_CHROMA_LEFT, "#.##");
RoundedFloatEditor plotLuminanceRightEditor(0.0f, 1.0f, 0.05f, DEFAULT_PLOT_LUMINANCE_RIGHT, "#.##");
RoundedFloatEditor plotChromaRightEditor(0.0f, 1.0f, 0.05f, DEFAULT_PLOT_CHROMA_RIGHT, "#.##");

// Fields in navigation order: left Luminance, left Chroma, right Luminance, right Chroma.
// Names are unique (for Preferences key derivation, see FieldEditor::_keyFor()) even
// though the left/right pairs are drawn under the shared "Luminance"/"Chroma" row labels
// in drawPlotColorsFields() below.
FieldEditor::FieldInfo plotColorsFields[] =
{
   { "Plot Luminance Left", &plotLuminanceLeftEditor },
   { "Plot Chroma Left", &plotChromaLeftEditor },
   { "Plot Luminance Right", &plotLuminanceRightEditor },
   { "Plot Chroma Right", &plotChromaRightEditor },
};
FieldEditor plotColorsEditor(&arduino, PREF_NAMESPACE, plotColorsFields);

// The generated palettes (see generatePlotColorsPalette()), each always exactly
// NUM_PLOT_COLORS_PER_SIDE entries, in ascending-hue order.
std::vector<Color> plotColorsPaletteLeft;
std::vector<Color> plotColorsPaletteRight;

// Left edge of the left set's bars, left edge of the right set's bars, top Y coordinate,
// and height of the Plotting Colors view's bars, computed in enterPlotColors() to center
// both fixed-width sets (with a gap between them) and fill the vertical space left over
// once the heading, marker row, and field table have been accounted for.
int16_t plotColorsLeftBarsLeft = 0;
int16_t plotColorsRightBarsLeft = 0;
int16_t plotColorsRefBarsLeft = 0;
int16_t plotColorsBarsTop = 0;
int16_t plotColorsBarHeight = 1;

// Top Y coordinate of the field table's rows (Luminance, then Chroma), and the X
// coordinates of the label column's left edge and each set's value column's left edge,
// all computed once in enterPlotColors().
int16_t plotColorsTableY = 0;
int16_t plotColorsLabelX = 0;
int16_t plotColorsLeftValueX = 0;
int16_t plotColorsRightValueX = 0;
int16_t plotColorsRowHeight = 1;

// Last R/G/B coefficients and per-side luminance/chroma values the palettes were solved
// against, so the loop() only needs to regenerate them when something relevant actually
// changed (Weighting itself is intentionally not tracked here - this view always solves
// under WEIGHTING_GAMMA, regardless of the Luminance view's own Weighting setting).
float lastPlotR = DEFAULT_R;
float lastPlotG = DEFAULT_G;
float lastPlotB = DEFAULT_B;
float lastPlotLuminanceLeft = DEFAULT_PLOT_LUMINANCE_LEFT;
float lastPlotChromaLeft = DEFAULT_PLOT_CHROMA_LEFT;
float lastPlotLuminanceRight = DEFAULT_PLOT_LUMINANCE_RIGHT;
float lastPlotChromaRight = DEFAULT_PLOT_CHROMA_RIGHT;

// plotColorsEditor's selected field index as of the last drawPlotColorsFields() call, and
// last-drawn text/colors for each plotColorsFields[] entry, so drawPlotColorsFields() only
// repaints a field when its text or color actually changed - same idea as
// lastHuesFieldText/huesFieldDrawn above.
uint8_t lastPlotColorsSelectedIndex = 0;
std::string lastPlotColorsFieldText[std::size(plotColorsFields)];
Color lastPlotColorsFieldColor[std::size(plotColorsFields)] = {};
Color lastPlotColorsFieldBackgroundColor[std::size(plotColorsFields)] = {};
bool plotColorsFieldDrawn[std::size(plotColorsFields)] = {};

///
/// <summary>
/// Generates one set's palette: one entry per PLOT_PRIMARY_SECONDARY_HUES hue (so this set
/// lines up hue-for-hue with the primary/secondary reference group), each solved (see
/// findLightnessForLuminance()) to hit `targetLuminance` at the given chroma (saturation),
/// under the current Red/Green/Blue coefficients and gamma weighting, so the whole set
/// reads as constant-luminance.
/// </summary>
/// <param name="chroma">Saturation (0.0-1.0) applied to every entry.</param>
/// <param name="targetLuminance">Target perceived brightness every entry is solved for.</param>
/// <param name="rWeight">Red coefficient.</param>
/// <param name="gWeight">Green coefficient.</param>
/// <param name="bWeight">Blue coefficient.</param>
/// <returns>The generated palette, with exactly NUM_PLOT_COLORS_PER_SIDE entries.</returns>
///
std::vector<Color> generatePlotColorsSet(float chroma, float targetLuminance, float rWeight, float gWeight, float bWeight)
{
   std::vector<Color> result;
   result.reserve(NUM_PLOT_COLORS_PER_SIDE);
   for (size_t i = 0; i < NUM_PLOT_COLORS_PER_SIDE; i++)
   {
      float hue = PLOT_PRIMARY_SECONDARY_HUES[i];
      float lightness = findLightnessForLuminance(hue, chroma, rWeight, gWeight, bWeight, WEIGHTING_GAMMA, targetLuminance);
      result.push_back(fromHSL(hue, chroma, lightness));
   }
   return result;
}

///
/// <summary>
/// Generates both sets' palettes (see generatePlotColorsSet()) from the live
/// plotColorsFields values and the current Red/Green/Blue coefficients, and records those
/// values so the loop() can detect the next actual change.
/// </summary>
///
void generatePlotColorsPalette()
{
   lastPlotR = rCoeffEditor.get();
   lastPlotG = gCoeffEditor.get();
   lastPlotB = bCoeffEditor.get();
   lastPlotLuminanceLeft = plotLuminanceLeftEditor.get();
   lastPlotChromaLeft = plotChromaLeftEditor.get();
   lastPlotLuminanceRight = plotLuminanceRightEditor.get();
   lastPlotChromaRight = plotChromaRightEditor.get();

   plotColorsPaletteLeft = generatePlotColorsSet(lastPlotChromaLeft, lastPlotLuminanceLeft, lastPlotR, lastPlotG, lastPlotB);
   plotColorsPaletteRight = generatePlotColorsSet(lastPlotChromaRight, lastPlotLuminanceRight, lastPlotR, lastPlotG, lastPlotB);
}

///
/// <summary>
/// Draws one set's row of PLOT_BAR_WIDTH-wide bars, spaced PLOT_BAR_GAP pixels apart,
/// starting at `left`, plus a matching row of marker dots PLOT_MARKER_OFFSET pixels above -
/// same idea as the Luminance view's marker row (a single colored pixel, against an
/// otherwise black background).
/// </summary>
/// <param name="left">Left edge of the set's first bar.</param>
/// <param name="palette">The set's colors to draw.</param>
///
void drawPlotColorsSet(int16_t left, const std::vector<Color>& palette)
{
   constexpr int16_t dotSize = 1;
   int16_t markerTop = plotColorsBarsTop - PLOT_MARKER_OFFSET - dotSize;

   for (size_t index = 0; index < palette.size(); index++)
   {
      int16_t x = left + (int16_t)index * (PLOT_BAR_WIDTH + PLOT_BAR_GAP);
      Color color = palette[index];

      arduino.fillRect(x, markerTop, PLOT_BAR_WIDTH, dotSize, Color::BLACK);
      arduino.fillRect(x, markerTop, dotSize, dotSize, color);

      arduino.fillRect(x, plotColorsBarsTop, PLOT_BAR_WIDTH, plotColorsBarHeight, color);
   }
}

///
/// <summary>
/// Draws all three sets' rows of bars (see drawPlotColorsSet()): the left and right
/// generated sets, plus the fixed primary/secondary reference colors to the far right.
/// </summary>
///
void drawPlotColorsRow()
{
   drawPlotColorsSet(plotColorsLeftBarsLeft, plotColorsPaletteLeft);
   drawPlotColorsSet(plotColorsRightBarsLeft, plotColorsPaletteRight);
   drawPlotColorsSet(plotColorsRefBarsLeft, PLOT_PRIMARY_SECONDARY_COLORS);
}

///
/// <summary>
/// Draws the Plotting Colors view's field table: a "Luminance"/"Chroma" label column on
/// the far left, then one value column per set (left set's values under the left set's
/// bars, right set's under the right set's), reading selection state from plotColorsEditor
/// to highlight whichever field is currently selected - the same idea as
/// FieldTableEditor's row highlighting, just laid out as two value columns instead of one.
/// </summary>
///
void drawPlotColorsFields()
{
   uint8_t selected = plotColorsEditor.selectedIndex();

   arduino.setTextSize(CALIBRATION_TEXT_SIZE);

   auto drawField = [&](uint8_t fieldIndex, int16_t x, int16_t y, int16_t w, int16_t h, const std::string& text, Color valueColor, Color backgroundColor)
   {
      if (plotColorsFieldDrawn[fieldIndex] && text == lastPlotColorsFieldText[fieldIndex] &&
         valueColor == lastPlotColorsFieldColor[fieldIndex] && backgroundColor == lastPlotColorsFieldBackgroundColor[fieldIndex])
      {
         return;
      }

      arduino.fillRect(x, y, w, h, Color::BLACK);
      arduino.setCursor(x, y);
      arduino.print(text.c_str(), valueColor, backgroundColor);

      lastPlotColorsFieldText[fieldIndex] = text;
      lastPlotColorsFieldColor[fieldIndex] = valueColor;
      lastPlotColorsFieldBackgroundColor[fieldIndex] = backgroundColor;
      plotColorsFieldDrawn[fieldIndex] = true;
   };

   constexpr uint8_t LEFT_LUM = 0;
   constexpr uint8_t LEFT_CHROMA = 1;
   constexpr uint8_t RIGHT_LUM = 2;
   constexpr uint8_t RIGHT_CHROMA = 3;

   int16_t lumRowY = plotColorsTableY;
   int16_t chromaRowY = plotColorsTableY + plotColorsRowHeight;

   // Row labels, drawn once (they never change/need highlighting).
   arduino.setCursor(plotColorsLabelX, lumRowY);
   arduino.print("Luminance", Color::LABEL);
   arduino.setCursor(plotColorsLabelX, chromaRowY);
   arduino.print("Chroma", Color::LABEL);

   auto drawValueField = [&](uint8_t fieldIndex, int16_t x, int16_t y)
   {
      ValueBase* value = plotColorsFields[fieldIndex].value;
      Color valueColor;
      Color backgroundColor;
      plotColorsEditor.colorsFor(fieldIndex, valueColor, backgroundColor);
      drawField(fieldIndex, x, y, arduino.textWidth("0.00"), arduino.charH(), value->valueText(), valueColor, backgroundColor);
   };

   drawValueField(LEFT_LUM, plotColorsLeftValueX, lumRowY);
   drawValueField(LEFT_CHROMA, plotColorsLeftValueX, chromaRowY);
   drawValueField(RIGHT_LUM, plotColorsRightValueX, lumRowY);
   drawValueField(RIGHT_CHROMA, plotColorsRightValueX, chromaRowY);

   lastPlotColorsSelectedIndex = selected;
}

///
/// <summary>
/// Lays out and draws the Plotting Colors view: places the fixed primary/secondary
/// reference colors on the far left, then the two generated sets of NUM_PLOT_COLORS_PER_SIDE
/// bars to their right (with PLOT_GROUP_GAP between all three groups), computes the field
/// table's position directly below the bars - with the label column and each set's value
/// column aligned under that same set's bars - generates both sets' palettes, then draws
/// the bars and field table.
/// </summary>
///
void enterPlotColors()
{
   // Force every field to be fully redrawn: enterView() just cleared the display, so the
   // "skip unchanged fields" tracking in drawPlotColorsFields() (plotColorsFieldDrawn[])
   // would otherwise think each field's text/color is unchanged from before the clear and
   // skip redrawing it, leaving the fields blank.
   std::fill(std::begin(plotColorsFieldDrawn), std::end(plotColorsFieldDrawn), false);

   int16_t setWidth = (int16_t)NUM_PLOT_COLORS_PER_SIDE * (PLOT_BAR_WIDTH + PLOT_BAR_GAP) - PLOT_BAR_GAP;
   int16_t refWidth = (int16_t)NUM_PLOT_PRIMARY_SECONDARY_COLORS * (PLOT_BAR_WIDTH + PLOT_BAR_GAP) - PLOT_BAR_GAP;
   int16_t totalWidth = refWidth + 2 * setWidth + 2 * PLOT_GROUP_GAP;
   plotColorsRefBarsLeft = (int16_t)((arduino.width() - totalWidth) / 2);
   plotColorsLeftBarsLeft = plotColorsRefBarsLeft + refWidth + PLOT_GROUP_GAP;
   plotColorsRightBarsLeft = plotColorsLeftBarsLeft + setWidth + PLOT_GROUP_GAP;

   generatePlotColorsPalette();

   arduino.setTextSize(CALIBRATION_TEXT_SIZE);
   plotColorsRowHeight = arduino.charH() + BAR_GAP;

   // Reserve space at the bottom for the two-row field table (plus a margin above it),
   // then fill whatever vertical space is left over with the bars. PLOT_MARKER_OFFSET is
   // used twice here: once between the subheading and the marker dots, and again between
   // the marker dots and the bars, so the spacing above and below the dots is equal.
   int16_t tableHeight = 2 * plotColorsRowHeight;
   int16_t fixedHeight = 2 * PLOT_MARKER_OFFSET + HEADING_MARGIN + tableHeight;
   plotColorsBarHeight = std::max((int16_t)((int16_t)arduino.height() - contentTop - fixedHeight), (int16_t)1);
   plotColorsBarsTop = contentTop + 2 * PLOT_MARKER_OFFSET;

   plotColorsTableY = plotColorsBarsTop + plotColorsBarHeight + HEADING_MARGIN;
   // The label column sits under the reference group (there are no editable fields for it),
   // and each set's value column is aligned directly under that set's own bars, so the
   // whole field table lines up with the three color groups above it.
   plotColorsLabelX = plotColorsRefBarsLeft;
   plotColorsLeftValueX = plotColorsLeftBarsLeft;
   plotColorsRightValueX = plotColorsRightBarsLeft;

   drawPlotColorsRow();
   drawPlotColorsFields();
}

///
/// <summary>
/// Clears the display, draws the shared "Colors" heading plus the current view's
/// subheading, records contentTop, then dispatches to that view's own setup/draw.
/// </summary>
///
void enterView()
{
   arduino.clearDisplay();

   arduino.setTextSize(HEADING_TEXT_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("Colors", Color::HEADING);

   arduino.setTextSize(SUBHEADING_TEXT_SIZE);
   arduino.println(VIEWS[(uint8_t)currentView].name, Color::SUB_HEADING);

   contentTop = arduino.getCursorY() + HEADING_MARGIN;

   switch (currentView)
   {
   case View::HuesByValue:
      huesByValueGrid.enter();
      break;
   case View::Hues:
      huesGrid.enter();
      break;
   case View::HuesByLuminance:
      huesLumGrid.enter();
      break;
   case View::Luminance:
      enterLuminance();
      break;
   case View::HuesBySaturation:
      huesSatGrid.enter();
      break;
   case View::PlottingColors:
      enterPlotColors();
      break;
   }
}

void setup()
{
   SerialX::begin();
   arduino.begin();
   arduino.buttonA.begin();
   arduino.buttonB.begin();

   calibrationEditor.load();
   huesByValueGrid.load();
   huesGrid.load();
   huesLumGrid.load();
   huesSatGrid.load();
   plotColorsEditor.load();

   for (size_t i = 0; i < NUM_REFERENCE_FORMULAS; i++)
   {
      referenceTable.setValue(0, i, REFERENCE_FORMULAS[i].r);
      referenceTable.setValue(1, i, REFERENCE_FORMULAS[i].g);
      referenceTable.setValue(2, i, REFERENCE_FORMULAS[i].b);
   }

   // The grid always shows exactly MAX_BARS columns (see MAX_BARS's comment for why), with
   // barWidth computed here to stretch those columns across as much of the display width
   // as possible. Shared by both views since they lay out the same number of columns
   // across the same display width.
   int16_t availableWidth = (int16_t)arduino.width();
   numBars = MAX_BARS;
   barWidth = std::max((int16_t)1, (int16_t)((availableWidth + BAR_GAP) / (int16_t)numBars - BAR_GAP));

   int16_t barsWidth = numBars * (barWidth + BAR_GAP) - BAR_GAP;
   barsLeft = (int16_t)((arduino.width() - barsWidth) / 2);

   enterView();

   arduino.encoderA.reset();
   arduino.encoderB.reset();
}

void loop()
{
   if (arduino.buttonA.wasPressed())
   {
      currentView = (View)(((uint8_t)currentView + 1) % NUM_VIEWS);
      enterView();
   }
   else if (arduino.buttonB.wasPressed())
   {
      currentView = (View)(((uint8_t)currentView + NUM_VIEWS - 1) % NUM_VIEWS);
      enterView();
   }

   if (currentView == View::HuesByValue)
   {
      huesByValueGrid.loop();
   }
   else if (currentView == View::Hues)
   {
      huesGrid.loop();
   }
   else if (currentView == View::HuesByLuminance)
   {
      huesLumGrid.loop();
   }
   else if (currentView == View::Luminance)
   {
      calibrationEditor.selectNext(arduino.encoderA.delta());

      int32_t adjustDelta = arduino.encoderB.delta();
      if (adjustDelta != 0)
      {
         calibrationEditor.adjustSelected(adjustDelta);
         calibrationEditor.save();
      }

      if (arduino.encoderB.button.wasPressed())
      {
         calibrationEditor.reset();
      }

      if (arduino.encoderA.button.wasPressed())
      {
         sortReversed = !sortReversed;
         recomputeLuminance();
         drawLuminanceRow();
      }

      calibrationEditor.draw();
      referenceTable.draw();

      if (lumSaturation1Editor.get() != lastLumSaturation1 || lumSaturation2Editor.get() != lastLumSaturation2 ||
         lumLightness1Editor.get() != lastLumLightness1 || lumLightness2Editor.get() != lastLumLightness2)
      {
         luminancePalette = generateLuminancePalette(luminanceNumBars);
         luminanceValues.resize(luminancePalette.size());

         lastLumSaturation1 = lumSaturation1Editor.get();
         lastLumSaturation2 = lumSaturation2Editor.get();
         lastLumLightness1 = lumLightness1Editor.get();
         lastLumLightness2 = lumLightness2Editor.get();

         recomputeLuminance();
         drawLuminanceRow();
      }
      else if (weightingEditor.get() != lastWeighting || rCoeffEditor.get() != lastR ||
         gCoeffEditor.get() != lastG || bCoeffEditor.get() != lastB)
      {
         recomputeLuminance();
         drawLuminanceRow();
      }
   }
   else if (currentView == View::HuesBySaturation)
   {
      huesSatGrid.loop();
   }
   else if (currentView == View::PlottingColors)
   {
      bool plotColorsFieldsChanged = plotColorsEditor.loop();

      bool plotColorsChanged = rCoeffEditor.get() != lastPlotR || gCoeffEditor.get() != lastPlotG || bCoeffEditor.get() != lastPlotB ||
         plotLuminanceLeftEditor.get() != lastPlotLuminanceLeft || plotChromaLeftEditor.get() != lastPlotChromaLeft ||
         plotLuminanceRightEditor.get() != lastPlotLuminanceRight || plotChromaRightEditor.get() != lastPlotChromaRight;

      if (plotColorsChanged)
      {
         generatePlotColorsPalette();
         drawPlotColorsRow();
         plotColorsFieldsChanged = true;
      }

      if (plotColorsFieldsChanged)
      {
         drawPlotColorsFields();
      }
   }
}
