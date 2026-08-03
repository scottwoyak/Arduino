//
// Color Calibrator Playground: two views, paged back and forth with Button A (next) /
// Button B (previous), same interaction pattern as Table_Playground. A third view is
// planned; the View enum/VIEW_INFO table below is already set up to make adding it easy.
//
// View 1 "Hues" - a single grid of HSL-based color bars, laid out left to right in
// ascending hue order (no sorting/weighting/RGB controls - this view is just about seeing
// the spectrum of hues). Each bar column is a stack of NUM_LIGHTNESS_LEVELS blocks showing
// the same hue at increasing lightness top to bottom (20%, 40%, 60%, 80% - 100% is omitted
// since it's just white regardless of hue). Above the grid, a matching stack of marker
// rows (one per lightness level, same order) shows only a single colored pixel in each
// level's color, horizontally centered in the column, against an otherwise black
// background - making it easier to pick out a single column's hue when columns are only a
// couple of pixels wide. Below the grid is a one-row table for the palettes' shared
// Saturation (chroma), editable live with Encoder B (Encoder A just selects the field,
// since it's the only one).
//
// View 2 "Luminance" - a single row of narrow (5 pixel wide) color bars generated from a
// spread of hue, saturation, and lightness combinations (see generateLuminancePalette()),
// packed as densely as possible across the display so many combinations can be compared at
// once, sorted by perceived brightness using the calibration table below (lightest on the
// left). A matching row of marker dots is shown above the bars, same idea as the Hues
// view's marker rows. Below the row is a table, editable live with the encoders (same
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

// Local library headers (from libraries/Woyak)
// ESP32_S3_Playground.h must come first so LGFX/LGFX_Sprite are defined before any
// header that transitively includes ArduinoWithDisplay.h.
#include "ESP32_S3_Playground.h"
#include "SerialX.h"
#include "Util.h"
#include "ArduinoBoard.h"
#include "FieldTable.h"
#include "FieldTableEditor.h"
#include "ValueEditor.h"
#include "Table.h"

#include <algorithm>
#include <cmath>
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
enum class View : uint8_t { Hues, Luminance };
constexpr uint8_t NUM_VIEWS = 2;

struct ViewInfo
{
   const char* name;
};

constexpr ViewInfo VIEWS[NUM_VIEWS] =
{
   { "Hues" },
   { "Luminance Sorting" },
};

View currentView = View::Hues;

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

///
/// <summary>
/// Converts HSL color components to a Color, since Color.h only provides an HSV
/// conversion. Standard HSL->RGB formula.
/// </summary>
/// <param name="hue">Hue, in degrees (0.0-360.0).</param>
/// <param name="saturation">Saturation (0.0-1.0).</param>
/// <param name="lightness">Lightness (0.0-1.0).</param>
/// <returns>The corresponding Color.</returns>
///
Color fromHSL(float hue, float saturation, float lightness)
{
   float c = (1.0f - fabsf(2.0f * lightness - 1.0f)) * saturation;
   float x = c * (1.0f - fabsf(fmodf(hue / 60.0f, 2.0f) - 1.0f));
   float m = lightness - (c / 2.0f);

   float r1, g1, b1;
   if (hue < 60.0f)
   {
      r1 = c; g1 = x; b1 = 0.0f;
   }
   else if (hue < 120.0f)
   {
      r1 = x; g1 = c; b1 = 0.0f;
   }
   else if (hue < 180.0f)
   {
      r1 = 0.0f; g1 = c; b1 = x;
   }
   else if (hue < 240.0f)
   {
      r1 = 0.0f; g1 = x; b1 = c;
   }
   else if (hue < 300.0f)
   {
      r1 = x; g1 = 0.0f; b1 = c;
   }
   else
   {
      r1 = c; g1 = 0.0f; b1 = x;
   }

   uint8_t red = (uint8_t)constrain((r1 + m) * 255.0f, 0.0f, 255.0f);
   uint8_t green = (uint8_t)constrain((g1 + m) * 255.0f, 0.0f, 255.0f);
   uint8_t blue = (uint8_t)constrain((b1 + m) * 255.0f, 0.0f, 255.0f);

   return Color565::fromRGB(red, green, blue);
}

///
/// <summary>
/// Decodes a single gamma-encoded sRGB channel (0.0-1.0) to linear light, using the
/// standard sRGB EOTF (a near-2.2 power curve with a linear toe near black).
/// </summary>
/// <param name="c">Gamma-encoded channel value (0.0-1.0).</param>
/// <returns>Linear-light channel value (0.0-1.0).</returns>
///
float srgbToLinear(float c)
{
   return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

///
/// <summary>
/// Computes a Color's perceived brightness using the given coefficients and weighting
/// mode, normalized to roughly 0.0-1.0, for use as a sort key. Weighting is applied on top
/// of the coefficients: "None" sums the raw gamma-encoded channels as-is (the traditional
/// approach, but not perceptually linear); "Gamma-corrected" decodes sRGB gamma to linear
/// light before summing (proper linear relative luminance) - both of these combine the
/// weighted channels with a plain sum, so they're monotonic re-expressions of each other
/// and never actually change sort order relative to one another. "HSP" instead sums the
/// weighted squares of the gamma-encoded channels (then takes the square root), which is
/// not a monotonic re-expression of a plain weighted sum, so it can rank colors of
/// different hues differently than the other two modes.
/// </summary>
/// <param name="color">The color to convert.</param>
/// <param name="rWeight">Red coefficient.</param>
/// <param name="gWeight">Green coefficient.</param>
/// <param name="bWeight">Blue coefficient.</param>
/// <param name="weighting">Which weighting mode to apply (WEIGHTING_NONE/GAMMA/HSP).</param>
/// <returns>The perceived brightness.</returns>
///
float toLuminance(Color color, float rWeight, float gWeight, float bWeight, long weighting);

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
constexpr long WEIGHTING_NONE = 0;
constexpr long WEIGHTING_GAMMA = 1;
constexpr long WEIGHTING_HSP = 2;

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
/// lightness, so only hue varies. Used by the Hues view - every lightness level's palette
/// shares the same hue-to-column mapping, so the stacked blocks line up in each column.
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

float toLuminance(Color color, float rWeight, float gWeight, float bWeight, long weighting)
{
   float r = Color565::getR(color) / 255.0f;
   float g = Color565::getG(color) / 255.0f;
   float b = Color565::getB(color) / 255.0f;

   if (weighting == WEIGHTING_HSP)
   {
      return std::sqrt((rWeight * r * r) + (gWeight * g * g) + (bWeight * b * b));
   }

   if (weighting == WEIGHTING_GAMMA)
   {
      r = srgbToLinear(r);
      g = srgbToLinear(g);
      b = srgbToLinear(b);
   }

   return (rWeight * r) + (gWeight * g) + (bWeight * b);
}

// ----------- Hues view: a single set of NUM_LIGHTNESS_LEVELS generated color palettes
// (see generateHuesPalettes()), one per lightness level at the saturation set by this
// view's one-row calibration table, all sharing the same hue-to-column mapping and left
// in ascending-hue order (no sorting/weighting).

// Default lightness levels shown as the stacked blocks within each bar column, top to
// bottom. 100% is omitted since it's just white regardless of hue/saturation. Ordered
// ascending so the smallest (darkest/lowest-luminance) level is on top. These are now
// editable live via huesCalibrationEditor's "Lightness 1".."Lightness N" rows (see
// huesLightnessEditors below); this array only supplies their default values.
constexpr float DEFAULT_LIGHTNESS_LEVELS[] = { 0.20f, 0.40f, 0.60f, 0.80f };
constexpr size_t NUM_LIGHTNESS_LEVELS = sizeof(DEFAULT_LIGHTNESS_LEVELS) / sizeof(DEFAULT_LIGHTNESS_LEVELS[0]);

// Each lightness level's generated palette (see generateHuesPalettes()), sized to fill
// exactly numBars bar positions each.
std::vector<Color> huesPalettes[NUM_LIGHTNESS_LEVELS];

// Saturation (chroma) applied to every entry in every huesPalettes level, editable live
// via huesCalibrationEditor (Encoder B).
RoundedFloatEditor saturationEditor(0.0f, 1.0f, 0.05f, DEFAULT_SATURATION, "#.##");

// One editable lightness value per stacked row, defaulted from DEFAULT_LIGHTNESS_LEVELS,
// each exposed as its own "Lightness N" row in huesCalibrationEditor below.
RoundedFloatEditor huesLightnessEditors[NUM_LIGHTNESS_LEVELS] =
{
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LIGHTNESS_LEVELS[0], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LIGHTNESS_LEVELS[1], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LIGHTNESS_LEVELS[2], "#.##"),
   RoundedFloatEditor(0.0f, 1.0f, 0.05f, DEFAULT_LIGHTNESS_LEVELS[3], "#.##"),
};

// Labels for each huesLightnessEditors row, e.g. "Lightness 1", "Lightness 2", etc.
const char* const HUES_LIGHTNESS_LABELS[] = { "Lightness 1", "Lightness 2", "Lightness 3", "Lightness 4" };

FieldTableEditor::Row huesCalibrationRows[] =
{
   { "Saturation", &saturationEditor },
   { HUES_LIGHTNESS_LABELS[0], &huesLightnessEditors[0] },
   { HUES_LIGHTNESS_LABELS[1], &huesLightnessEditors[1] },
   { HUES_LIGHTNESS_LABELS[2], &huesLightnessEditors[2] },
   { HUES_LIGHTNESS_LABELS[3], &huesLightnessEditors[3] },
};
FieldTableEditor huesCalibrationEditor(&arduino, PREF_NAMESPACE, huesCalibrationRows, CALIBRATION_TEXT_SIZE);

// Saturation as of the last recompute, used to detect when huesCalibrationEditor has
// changed and the palettes need to be regenerated.
float lastHuesSaturation = DEFAULT_SATURATION;

// Lightness values as of the last recompute, used to detect when huesCalibrationEditor's
// Lightness rows have changed and the palettes need to be regenerated.
float lastHuesLightness[NUM_LIGHTNESS_LEVELS] =
{
   DEFAULT_LIGHTNESS_LEVELS[0], DEFAULT_LIGHTNESS_LEVELS[1], DEFAULT_LIGHTNESS_LEVELS[2], DEFAULT_LIGHTNESS_LEVELS[3]
};

// Width reserved on the left of the grid for each row's lightness value label, computed
// once in enterHues() from the widest formatted label at CALIBRATION_TEXT_SIZE.
int16_t huesLabelWidth = 0;

// Format for the Hues view's left-side lightness value labels, e.g. "100%".
Format huesLabelFormat("###%");

// Top Y coordinate of the Hues view's bars, and the height of a single stacked block /
// the whole stack, computed in enterHues() to fill the vertical space left over once the
// heading, marker rows, and huesCalibrationEditor have been accounted for.
int16_t huesBarsTop = 0;
int16_t huesBlockHeight = 1;
int16_t huesBarHeight = 1;

///
/// <summary>
/// Generates every lightness level's palette for the Hues view, each sized to exactly
/// `count` entries, at the live saturation/lightness values from huesCalibrationEditor, so
/// they all share the same hue-to-column mapping.
/// </summary>
/// <param name="count">Exact number of colors to generate per palette.</param>
///
void generateHuesPalettes(size_t count)
{
   lastHuesSaturation = saturationEditor.get();
   for (size_t lIndex = 0; lIndex < NUM_LIGHTNESS_LEVELS; lIndex++)
   {
      lastHuesLightness[lIndex] = huesLightnessEditors[lIndex].get();
      huesPalettes[lIndex] = generateHuePalette(count, lastHuesSaturation, lastHuesLightness[lIndex]);
   }
}

///
/// <summary>
/// Draws every bar column in the Hues view's grid as a stack of NUM_LIGHTNESS_LEVELS
/// blocks (one per huesLightnessEditors value, top to bottom - darkest on top), separated
/// by BAR_GAP, plus a matching stack of marker rows BAR_MARKER_OFFSET pixels above the grid
/// (one row per lightness level, same order/spacing as the blocks below). Each marker row
/// is left black except for a single pixel (never larger, regardless of column/block
/// size) centered within that row, in that level's color - unlike the solid blocks below,
/// so the dot's a clear point rather than adding another solid block of color. Columns are
/// left in ascending-hue order (no sorting). To the left of the grid, draws each row's
/// current lightness value (e.g. "80%") as a label, vertically centered on that row.
/// </summary>
///
void drawHuesGrid()
{
   constexpr int16_t dotSize = 1;
   int16_t markerHeight = (int16_t)NUM_LIGHTNESS_LEVELS * (huesBlockHeight + BAR_GAP) - BAR_GAP;
   int16_t markerTop = huesBarsTop - BAR_MARKER_OFFSET - markerHeight;

   // Left-side lightness value labels, one per row, vertically centered on that row.
   arduino.setTextSize(CALIBRATION_TEXT_SIZE);
   int16_t barY = huesBarsTop;
   for (size_t lIndex = 0; lIndex < NUM_LIGHTNESS_LEVELS; lIndex++)
   {
      arduino.fillRect(0, barY, huesLabelWidth - HUES_LABEL_GAP, huesBlockHeight, Color::BLACK);
      arduino.setCursor(0, barY + (huesBlockHeight - arduino.charH()) / 2);
      arduino.print((int)std::round(lastHuesLightness[lIndex] * 100.0f), huesLabelFormat, Color::VALUE);
      barY += huesBlockHeight + BAR_GAP;
   }

   for (uint16_t index = 0; index < numBars; index++)
   {
      int16_t x = barsLeft + index * (barWidth + BAR_GAP);
      int16_t dotX = x + (barWidth - dotSize) / 2;

      arduino.fillRect(x, markerTop, barWidth, markerHeight, Color::BLACK);

      int16_t markerY = markerTop;
      int16_t barY2 = huesBarsTop;
      for (size_t lIndex = 0; lIndex < NUM_LIGHTNESS_LEVELS; lIndex++)
      {
         Color color = huesPalettes[lIndex][index];
         arduino.fillRect(dotX, markerY + (huesBlockHeight - dotSize) / 2, dotSize, dotSize, color);
         arduino.fillRect(x, barY2, barWidth, huesBlockHeight, color);
         markerY += huesBlockHeight + BAR_GAP;
         barY2 += huesBlockHeight + BAR_GAP;
      }
   }
}

///
/// <summary>
/// Lays out and draws the Hues view: computes huesLabelWidth, huesBarsTop/huesBlockHeight/
/// huesBarHeight to fill the space between the heading/markers and huesCalibrationEditor
/// (positioned at the bottom of the grid so it never overlaps the bars), generates the
/// palettes, then draws the grid.
/// </summary>
///
void enterHues()
{
   generateHuesPalettes(numBars);

   // Reserve space on the left for each row's lightness value label ("100%" is the widest
   // possible value), then recompute barWidth/barsLeft (originally sized in setup() for
   // the full display width) to fill only the remaining width to its right, so the grid
   // no longer overlaps the labels.
   arduino.setTextSize(CALIBRATION_TEXT_SIZE);
   huesLabelWidth = arduino.textWidth("100%") + HUES_LABEL_GAP;

   int16_t availableWidth = (int16_t)arduino.width() - huesLabelWidth;
   barWidth = std::max((int16_t)1, (int16_t)((availableWidth + BAR_GAP) / (int16_t)numBars - BAR_GAP));
   int16_t barsWidth = numBars * (barWidth + BAR_GAP) - BAR_GAP;
   barsLeft = huesLabelWidth + (int16_t)((availableWidth - barsWidth) / 2);

   // Compute the block height that fills whatever vertical space is left over once the
   // content top, the marker dot rows (BAR_MARKER_OFFSET + one dot-height stack, same
   // height as the grid itself), the grid, and huesCalibrationEditor have all been
   // accounted for.
   int16_t fixedHeight = BAR_MARKER_OFFSET + HEADING_MARGIN + HEADING_MARGIN + huesCalibrationEditor.height();
   int16_t availableHeight = (int16_t)arduino.height() - contentTop - fixedHeight;
   huesBlockHeight = std::max((int16_t)((availableHeight + BAR_GAP) / (2 * (int16_t)NUM_LIGHTNESS_LEVELS) - BAR_GAP), (int16_t)1);
   huesBarHeight = NUM_LIGHTNESS_LEVELS * (huesBlockHeight + BAR_GAP) - BAR_GAP;

   huesBarsTop = contentTop + BAR_MARKER_OFFSET + huesBarHeight;

   // Always position the table directly below the actual bottom of the grid, rather than
   // relying on the (potentially inexact, due to integer rounding above) space-filling
   // math to land exactly there - this guarantees the table never overlaps the bars.
   huesCalibrationEditor.setPosition(0, huesBarsTop + huesBarHeight + HEADING_MARGIN);

   drawHuesGrid();
   huesCalibrationEditor.draw();
}

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
/// Generates the Luminance view's palette: every combination of LUM_SATURATION_LEVELS and
/// LUM_LIGHTNESS_LEVELS, each crossed with an even spread of hues, flattened into a single
/// vector sized to exactly `count` entries (see the assumption documented on
/// LUM_SATURATION_LEVELS above). Final on-screen order comes from sorting by luminance
/// (see recomputeLuminance()), so the order colors are generated in here doesn't matter.
/// </summary>
/// <param name="count">Exact number of colors to generate.</param>
/// <returns>The generated palette, with exactly `count` entries.</returns>
///
std::vector<Color> generateLuminancePalette(size_t count)
{
   std::vector<Color> result;
   result.reserve(count);

   float saturationLevels[NUM_LUM_SATURATION_LEVELS] = { lumSaturation1Editor.get(), lumSaturation2Editor.get() };
   float lightnessLevels[NUM_LUM_LIGHTNESS_LEVELS] = { lumLightness1Editor.get(), lumLightness2Editor.get() };

   size_t combos = NUM_LUM_SATURATION_LEVELS * NUM_LUM_LIGHTNESS_LEVELS;
   size_t huesPerCombo = count / combos;

   for (size_t sIndex = 0; sIndex < NUM_LUM_SATURATION_LEVELS; sIndex++)
   {
      for (size_t lIndex = 0; lIndex < NUM_LUM_LIGHTNESS_LEVELS; lIndex++)
      {
         for (size_t hIndex = 0; hIndex < huesPerCombo; hIndex++)
         {
            float hue = 360.0f * (float)hIndex / (float)huesPerCombo;
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
   case View::Hues:
      enterHues();
      break;
   case View::Luminance:
      enterLuminance();
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
   huesCalibrationEditor.load();

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

   if (currentView == View::Hues)
   {
      huesCalibrationEditor.selectNext(arduino.encoderA.delta());

      int32_t adjustDelta = arduino.encoderB.delta();
      if (adjustDelta != 0)
      {
         huesCalibrationEditor.adjustSelected(adjustDelta);
         huesCalibrationEditor.save();
      }

      if (arduino.encoderB.button.wasPressed())
      {
         huesCalibrationEditor.reset();
      }

      huesCalibrationEditor.draw();

      bool huesChanged = saturationEditor.get() != lastHuesSaturation;
      for (size_t lIndex = 0; lIndex < NUM_LIGHTNESS_LEVELS; lIndex++)
      {
         if (huesLightnessEditors[lIndex].get() != lastHuesLightness[lIndex])
         {
            huesChanged = true;
         }
      }

      if (huesChanged)
      {
         generateHuesPalettes(numBars);
         drawHuesGrid();
      }
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
}
