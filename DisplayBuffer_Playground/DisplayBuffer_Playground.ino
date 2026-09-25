//
// Benchmarks DisplayBuffer redraw performance by continuously swapping between two randomly
// generated pixel images and measuring the resulting frame rate.
//
// A single DisplayBuffer is bound to the area right of the editable status table. Two target
// images (flat layer-index arrays) are generated up front - and regenerated whenever Num
// Colors or Num Points change - each with a caller-specified number of randomly-placed shaded
// pixels evenly distributed across the available colors. loop() alternates stamping each
// target image into
// the buffer and redrawing it using the selected method:
//   - Full: forces a full repaint every frame (draw() every pixel, changed or not).
//   - Diff: only repaints pixels whose layer actually changed since the last frame.
//

// System/standard library headers
#include <array>
#include <span>
#include <Wire.h>

// Local library headers (from libraries/Woyak)
// ESP32_S3_Playground.h must come first so LGFX/LGFX_Sprite are defined before
// DisplayBuffer.h/FieldTableEditor.h transitively include ArduinoWithDisplay.h.
#include "ESP32_S3_Playground.h"
#include "ArduinoBoard.h"
#include "DisplayBuffer.h"
#include "FieldTableEditor.h"
#include "Memory.h"
#include "SerialX.h"
#include "Timer.h"
#include "TimedRate.h"
#include "Util.h"
#include "ValueEditor.h"

constexpr const char* PREF_NAMESPACE = "DispBufPg";

// ----------- Display Geometry
// Matches the ESP32_S3_Playground board's LGX_Hosyond_ST7796 display in landscape orientation.
constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint16_t DISPLAY_HEIGHT = 320;
constexpr uint16_t TABLE_BUFFER_GAP = 10; // gap between the status table and the drawn buffer

// ----------- Num Colors Selection
constexpr long MIN_NUM_COLORS = 1;
constexpr long MAX_NUM_COLORS = DisplayBuffer::MAX_LAYERS;
constexpr long DEFAULT_NUM_COLORS = 4;

// ----------- Num Points Selection (number of shaded pixels to display, 1-50000)
constexpr long MIN_NUM_POINTS = 1;
constexpr long MAX_NUM_POINTS = 50000;
constexpr long DEFAULT_NUM_POINTS = 10000;

// ----------- Point Size Selection (marker size, in pixels; see DisplayBuffer::drawPoint())
constexpr long MIN_POINT_SIZE = 1;
constexpr long MAX_POINT_SIZE = 10;
constexpr long DEFAULT_POINT_SIZE = 1;

// A fixed palette of distinct colors, indexed by layer (1..MAX_LAYERS), assigned round-robin
// across the shaded pixels so colors are evenly distributed. Hues are generated with a
// bisecting (van der Corput base-2) sequence starting at lime's hue (120 degrees): the 2nd
// hue is diametrically opposite the 1st, the 3rd/4th bisect the two remaining half-circle
// gaps, and so on - each new hue always lands as far as possible (in degrees) from every hue
// chosen so far, and in particular from the immediately preceding one, rather than just being
// evenly spaced end-to-end. Colors use fromHSVLuminance() rather than fromHSV() at a fixed
// value/brightness, since a fixed HSV "value" makes hues like pure red/blue look much dimmer
// than green at the same nominal brightness (green contributes far more to perceived
// luminance) - fromHSVLuminance() instead targets equal *perceived* brightness across hues.
constexpr float PALETTE_SATURATION = 1.0f;
constexpr float PALETTE_LUMINANCE = 0.7f;
const std::array<Color, DisplayBuffer::MAX_LAYERS> PALETTE_COLORS =
{
   Color565::fromHSVLuminance(120.0f, PALETTE_SATURATION, PALETTE_LUMINANCE),   // Lime (green)
   Color565::fromHSVLuminance(300.0f, PALETTE_SATURATION, PALETTE_LUMINANCE),   // Magenta
   Color565::fromHSVLuminance(210.0f, PALETTE_SATURATION, PALETTE_LUMINANCE),   // Azure (sky blue)
   Color565::fromHSVLuminance(30.0f, PALETTE_SATURATION, PALETTE_LUMINANCE),    // Orange
   Color565::fromHSVLuminance(165.0f, PALETTE_SATURATION, PALETTE_LUMINANCE),   // Spring green (teal-green)
   Color565::fromHSVLuminance(345.0f, PALETTE_SATURATION, PALETTE_LUMINANCE),   // Rose (pink-red)
   Color565::fromHSVLuminance(255.0f, PALETTE_SATURATION, PALETTE_LUMINANCE),   // Blue-violet (indigo)
   Color565::fromHSVLuminance(75.0f, PALETTE_SATURATION, PALETTE_LUMINANCE),    // Chartreuse (yellow-green)
   Color565::fromHSVLuminance(142.5f, PALETTE_SATURATION, PALETTE_LUMINANCE),   // Sea green (teal-green)
   Color565::fromHSVLuminance(322.5f, PALETTE_SATURATION, PALETTE_LUMINANCE),   // Pink (magenta-pink)
   Color565::fromHSVLuminance(232.5f, PALETTE_SATURATION, PALETTE_LUMINANCE),   // Blue
   Color565::fromHSVLuminance(52.5f, PALETTE_SATURATION, PALETTE_LUMINANCE),    // Amber (gold/yellow-orange)
   Color565::fromHSVLuminance(187.5f, PALETTE_SATURATION, PALETTE_LUMINANCE),   // Teal (cyan-teal)
   Color565::fromHSVLuminance(7.5f, PALETTE_SATURATION, PALETTE_LUMINANCE),     // Red-orange
   Color565::fromHSVLuminance(277.5f, PALETTE_SATURATION, PALETTE_LUMINANCE),   // Purple
};

// ----------- Background Color Selection
const std::array<const char*, 5> BACKGROUND_LABELS = { "Black", "DarkGray", "Gray", "LightGray", "White" };
const std::array<Color, BACKGROUND_LABELS.size()> BACKGROUND_COLORS = { Color::BLACK, Color::DARKGRAY, Color::GRAY, Color::LIGHTGRAY, Color::WHITE };

// ----------- Redraw Method Selection
const std::array<const char*, 2> METHOD_LABELS = { "Full", "Diff" };

// ----------- The Board
ESP32_S3_Playground arduino;

// ----------- Input Fields
IntEditor numColorsEditor(MIN_NUM_COLORS, MAX_NUM_COLORS, 1, DEFAULT_NUM_COLORS, "#####");
ScaledStepIntEditor numPointsEditor(MIN_NUM_POINTS, MAX_NUM_POINTS, DEFAULT_NUM_POINTS, "#####");
ScaledStepIntEditor pointSizeEditor(MIN_POINT_SIZE, MAX_POINT_SIZE, DEFAULT_POINT_SIZE, "#####");
EnumEditor backgroundEditor(BACKGROUND_LABELS, 0, "########");
EnumEditor methodEditor(METHOD_LABELS, 0, "######");

// ----------- Output Fields
FloatValue widthField("#####");
FloatValue heightField("#####");
FloatValue pointCountField("#######");
FloatValue drawnPixelCountField("#######");
FloatValue densityField("##.#%");
FloatValue memoryField("###.# kb");
FloatValue fpsField("####/s");

FieldTableEditor::Row statusCells[] =
{
   { "Input" },
   { "Num Colors", &numColorsEditor },
   { "Num Points", &numPointsEditor },
   { "Point Size", &pointSizeEditor },
   { "Background", &backgroundEditor },
   { "Method", &methodEditor },
   { "Output" },
   { "Width", &widthField },
   { "Height", &heightField },
   { "Total Pixels", &pointCountField },
   { "Drawn Pixels", &drawnPixelCountField },
   { "Density", &densityField },
   { "Memory", &memoryField },
   { "FPS", &fpsField },
};
FieldTableEditor statusTable(&arduino, PREF_NAMESPACE, statusCells, DEFAULT_TEXT_SIZE);

// ----------- Buffer/Image State
DisplayBuffer buffer;
uint16_t headerHeight = 0;
uint16_t bufferLeft = 0;
uint16_t bufferWidth = 0;
uint16_t bufferHeight = 0;

// Flat row-major layer-index arrays (one byte per pixel) for the two target images that
// loop() alternates between. Allocated/regenerated whenever the buffer's size, Num Colors,
// or Num Points change. Both images are staged into the buffer just once, in regenerateImages()
// (via DisplayBuffer::drawImage()/swapBuffers()), so that loop() itself only needs to call
// DisplayBuffer::draw() each frame - it swaps buffers internally after drawing - no per-frame
// pixel stamping at all.
uint8_t* imageA = nullptr;
uint8_t* imageB = nullptr;

// True whenever the buffer's staged content no longer matches what's actually still
// physically on the display (e.g. regenerateImages() just re-staged two new images with
// different content than what was last drawn), so the next draw() call must unconditionally
// repaint every pixel rather than diffing the two staged images against each other.
bool pendingFullRepaint = false;

// True whenever an encoder-driven input change (Num Colors/Num Points/Point Size, or
// Background) still needs its corresponding rebuild - regenerateImages() or recreateBuffer()
// respectively - performed. Left true if a rebuild was abandoned partway through because the
// encoder moved again (see generateRandomImage()'s remarks), so loop() retries it on a later
// iteration instead of losing track of the pending rebuild.
bool imageRegenerationPending = false;
bool bufferRecreationPending = false;

TimedRate updateRate(1000UL);
Timer fpsUpdateTimer(500UL);
Memory memory;

///
/// <summary>
/// Recomputes memoryField's value from the current memory delta relative to memory's baseline.
/// </summary>
///
void updateMemoryReadout()
{
   memoryField.set((float)memory.delta() / 1024.0f);
}

///
/// <summary>
/// Stamps a single point marker into a flat row-major layer-index image, centered on (x, y):
/// a single pixel when size is 1 or less, otherwise a small filled circle whose radius is
/// size - 1, mirroring DisplayBuffer::drawPoint()'s algorithm exactly so the staged images
/// match what setPixel()/drawPoint() would draw directly into a DisplayBuffer. Pixels outside
/// the image bounds are silently skipped.
/// </summary>
/// <param name="image">Destination flat array of width * height bytes.</param>
/// <param name="width">Image width in pixels.</param>
/// <param name="height">Image height in pixels.</param>
/// <param name="x">Center column.</param>
/// <param name="y">Center row.</param>
/// <param name="size">Marker size, in pixels; sizes above 1 draw a circle of radius size - 1.</param>
/// <param name="colorIndex">Layer index (nonzero) to stamp the marker's pixels with.</param>
///
void drawPointInImage(uint8_t* image, uint32_t width, uint32_t height, int32_t x, int32_t y, long size, uint8_t colorIndex)
{
   auto setImagePixel = [&](int32_t px, int32_t py)
   {
      if (px >= 0 && px < (int32_t)width && py >= 0 && py < (int32_t)height)
      {
         image[((uint32_t)py * width) + (uint32_t)px] = colorIndex;
      }
   };

   if (size <= 1)
   {
      setImagePixel(x, y);
      return;
   }

   int32_t radius = (int32_t)size - 1;
   int32_t radiusSquared = radius * radius;

   for (int32_t dy = -radius; dy <= radius; dy++)
   {
      int32_t halfWidth = (int32_t)sqrtf((float)(radiusSquared - (dy * dy)));

      if (halfWidth == 0)
      {
         setImagePixel(x, y + dy);
      }
      else
      {
         for (int32_t dx = -halfWidth; dx <= halfWidth; dx++)
         {
            setImagePixel(x + dx, y + dy);
         }
      }
   }
}

///
/// <summary>
/// Fills one target image's flat layer-index array with the given number of randomly-placed
/// shaded points, evenly distributed across the available colors. Zeroes the whole array
/// first, then draws each point in a simple loop by independently rolling a random x and a
/// random y coordinate (two random() calls per point), stamping a pointSize-sized marker (see
/// drawPointInImage()) at each. pointCount is clamped to the image's total pixel count.
/// </summary>
/// <param name="image">Destination flat array of width * height bytes.</param>
/// <param name="width">Image width in pixels.</param>
/// <param name="height">Image height in pixels.</param>
/// <param name="numColors">Number of distinct colors (layers) to distribute evenly.</param>
/// <param name="pointCount">Number of randomly-placed points to shade.</param>
/// <param name="pointSize">Marker size, in pixels, for each point (see DisplayBuffer::drawPoint()).</param>
/// <returns>
/// True if every point was drawn; false if drawing was abandoned partway through because an
/// encoder moved (see remarks).
/// </returns>
/// <remarks>
/// Checks both encoders periodically (not on every single point - hasChanged() still costs a
/// digitalRead()-equivalent, and calling it per-point would noticeably slow down drawing) and
/// bails out early, leaving the image only partially filled, the moment either one moves. This
/// keeps a single call from blocking loop() long enough to miss encoder detents during a fast
/// spin (see generateRandomImage()'s remarks) - the caller is expected to notice the false
/// return and simply retry once the encoder settles, rather than treat the partial image as
/// final.
/// </remarks>
///
bool generateRandomImage(uint8_t* image, uint32_t width, uint32_t height, long numColors, long pointCount, long pointSize)
{
   constexpr uint32_t ENCODER_CHECK_INTERVAL = 256;

   uint32_t totalPixels = width * height;
   uint32_t clampedPointCount = min((uint32_t)pointCount, totalPixels);
   uint8_t nextColor = 0;

   memset(image, 0, totalPixels);

   for (uint32_t i = 0; i < clampedPointCount; i++)
   {
      if ((i % ENCODER_CHECK_INTERVAL) == 0 && (arduino.encoderA.hasChanged() || arduino.encoderB.hasChanged()))
      {
         return false;
      }

      uint32_t x = (uint32_t)random(0, width);
      uint32_t y = (uint32_t)random(0, height);

      drawPointInImage(image, width, height, (int32_t)x, (int32_t)y, pointSize, (uint8_t)(nextColor + 1));
      nextColor = (uint8_t)((nextColor + 1) % numColors);
   }

   return true;
}

///
/// <summary>
/// (Re)allocates and (re)fills both target images to match the buffer's current dimensions,
/// Num Colors, Num Points, and Point Size, then (re)assigns the buffer's palette colors.
/// Called once from setup() and whenever any of those inputs change.
/// </summary>
/// <returns>
/// True if both images were fully regenerated and staged; false if generation was abandoned
/// partway through because an encoder moved (see generateRandomImage()'s remarks) - the caller
/// should retry once the encoder settles rather than treat the buffer as up to date.
/// </returns>
///
bool regenerateImages()
{
   uint32_t totalPixels = (uint32_t)bufferWidth * bufferHeight;

   delete[] imageA;
   delete[] imageB;

   imageA = new (std::nothrow) uint8_t[totalPixels];
   imageB = new (std::nothrow) uint8_t[totalPixels];

   if (imageA == nullptr || imageB == nullptr)
   {
      Util::reset(0.0f, "OOM allocating DisplayBuffer_Playground images");
      return false;
   }

   std::span<const Color> activeColors(PALETTE_COLORS.data(), (size_t)numColorsEditor.get());
   for (size_t i = 0; i < activeColors.size(); i++)
   {
      buffer.setPaletteColor((uint8_t)(i + 1), activeColors[i]);
   }

   long pointCount = numPointsEditor.get();
   long pointSize = pointSizeEditor.get();

   if (!generateRandomImage(imageA, bufferWidth, bufferHeight, numColorsEditor.get(), pointCount, pointSize)
      || !generateRandomImage(imageB, bufferWidth, bufferHeight, numColorsEditor.get(), pointCount, pointSize))
   {
      return false;
   }

   // Stage both target images into the buffer just once, up front, rather than re-staging
   // whichever one is needed on every redraw: drawImage() packs imageA into the back buffer,
   // swapBuffers() makes it current, then drawImage() packs imageB into the new back buffer.
   // Buffer ends up with imageA current and imageB previous, ready for loop() to alternate
   // between them with nothing but swapBuffers() + a draw each frame.
   buffer.drawImage(imageA);
   buffer.swapBuffers();
   buffer.drawImage(imageB);

   // The physical display still shows whatever was drawn from the previous images, which may
   // not match either newly-staged image (e.g. Num Points just decreased), so diffing the two
   // new images against each other isn't a valid way to decide what needs repainting - force
   // the next draw() to repaint every pixel instead.
   pendingFullRepaint = true;

   uint32_t clampedPointCount = min((uint32_t)pointCount, totalPixels);
   drawnPixelCountField.set((float)clampedPointCount);
   densityField.set((totalPixels > 0) ? ((float)clampedPointCount * 100.0f / (float)totalPixels) : 0.0f);

   updateMemoryReadout();
   return true;
}

///
/// <summary>
/// Draws the "DisplayBuffer" heading and "Buffer Draw" subheading, recording the header
/// height that follows for layout of the status table and buffer area.
/// </summary>
///
void drawTitle()
{
   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   arduino.clearDisplay();
   arduino.println("DisplayBuffer", Color::HEADING);

   arduino.setTextSize(DEFAULT_TEXT_SIZE);
   arduino.println("Buffer Draw", Color::SUB_HEADING);

   headerHeight = arduino.getCursorY();
}

///
/// <summary>
/// Recomputes the buffer's position/size from the status table's current width and (re)binds
/// it, clearing the buffer's display area to the current background color. Called once from
/// setup() and whenever the buffer's dimensions or background color change.
/// </summary>
///
void recreateBuffer()
{
   bufferLeft = statusTable.width() + TABLE_BUFFER_GAP;
   bufferWidth = DISPLAY_WIDTH - bufferLeft;
   // The buffer's drawn area extends all the way to the top of the screen (y=0), overlapping
   // the title's row, rather than starting below headerHeight like the status table does -
   // this lets the buffer use as much vertical space as possible.
   bufferHeight = DISPLAY_HEIGHT;

   buffer.bind(&arduino, bufferLeft, 0, bufferWidth, bufferHeight);
   buffer.setBackgroundColor(BACKGROUND_COLORS[backgroundEditor.get()]);
   // The buffer's whole display region is physically painted to the background color right
   // here, so both the current and previous frame buffers can be reset to match it without
   // needing a full repaint on the next draw().
   arduino.fillRect(bufferLeft, 0, bufferWidth, bufferHeight, BACKGROUND_COLORS[backgroundEditor.get()]);
   buffer.clear(true);

   widthField.set((float)bufferWidth);
   heightField.set((float)bufferHeight);
   pointCountField.set((float)bufferWidth * bufferHeight);
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();
   statusTable.load();

   drawTitle();

   // Restore text size 2 (used by the status table) immediately after drawing the title at
   // size 3, since recreateBuffer() below measures the table's width via charW(), which
   // depends on the currently active text size.
   arduino.setTextSize(DEFAULT_TEXT_SIZE);
   statusTable.setPosition(0, headerHeight);

   memory.reset();

   recreateBuffer();
   regenerateImages();

   statusTable.draw();

   arduino.encoderA.reset();
   arduino.encoderB.reset();
}

void loop()
{
   if (arduino.buttonA.wasPressed())
   {
      Util::reset(0.0f, "Manual reset (button A)");
   }

   // While buttonB is held, slow the loop down with a delay so individual frames can be
   // visually monitored (e.g. to inspect Diff mode's redraw behavior frame by frame).
   if (arduino.buttonB.isPressed())
   {
      delay(500UL);
   }

   int32_t statusSelectDelta = arduino.encoderA.delta();
   int32_t statusAdjustDelta = arduino.encoderB.delta();
   if (statusSelectDelta != 0 || statusAdjustDelta != 0)
   {
      statusTable.selectNext(statusSelectDelta);
      statusTable.adjustSelected(statusAdjustDelta);

      statusTable.save();
      arduino.setTextSize(DEFAULT_TEXT_SIZE);

      // Call hasChanged() on every editor (not just short-circuited ones) so each editor's
      // internal baseline always stays current even when a different field is the one that
      // actually changed this tick. The bitwise (non-short-circuiting) | ensures that even
      // though only the first true result is needed below.
      if (numColorsEditor.hasChanged() | numPointsEditor.hasChanged() | pointSizeEditor.hasChanged())
      {
         imageRegenerationPending = true;
      }
      else if (backgroundEditor.hasChanged())
      {
         bufferRecreationPending = true;
      }

      statusTable.draw();
   }

   // generateRandomImage() (called from regenerateImages()) bails out early, leaving the
   // *Pending flag set, the moment it notices either encoder has moved - see its remarks -
   // so a pending rebuild simply gets retried on a later loop() iteration once the encoder
   // stops moving, rather than ever blocking long enough to drop detents.
   if (imageRegenerationPending)
   {
      imageRegenerationPending = !regenerateImages();
      if (!imageRegenerationPending)
      {
         statusTable.draw();
      }
   }
   else if (bufferRecreationPending)
   {
      recreateBuffer();

      // recreateBuffer() clears both of the buffer's frames, so the two target images
      // (otherwise only ever staged once, in regenerateImages()) must be re-staged here.
      buffer.drawImage(imageA);
      buffer.swapBuffers();
      buffer.drawImage(imageB);

      updateMemoryReadout();
      bufferRecreationPending = false;
      statusTable.draw();
   }

   // Redraws using the currently selected method - Full forces a full repaint of every
   // pixel; Diff only repaints pixels whose layer actually changed since the frame last
   // physically painted to the display - then alternates to the other already-staged target
   // image: draw() swaps the buffer's current/previous frames afterward (see
   // DisplayBuffer::draw()'s remarks), an O(1) pointer swap, since both target images were
   // already staged into the buffer once by regenerateImages(). pendingFullRepaint forces one
   // extra full repaint after regenerateImages() re-stages images that may not match what's
   // still physically on the display (see its remarks).
   bool isFullRepaint = (methodEditor.get() == 0) || pendingFullRepaint;
   DisplayBuffer::RedrawMode redrawMode = isFullRepaint
      ? DisplayBuffer::RedrawMode::FULL
      : DisplayBuffer::RedrawMode::DIFF;
   buffer.draw(redrawMode);
   pendingFullRepaint = false;

   updateRate.tick();
   if (fpsUpdateTimer.ready())
   {
      fpsField.set(updateRate.get());
   }
   statusTable.draw();
}
