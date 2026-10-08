//
// Sound
//
// Plays the notification sound on the Viewer board's speaker and provides touch sliders
// for tweaking the volume:
// - Volume: software playback gain applied to the sound samples.
// - Codec: the ES8311 codec's DAC volume register (150 - 200).
//
// The scrollable list on the right chooses the sound and plays it each time a sound name
// is pressed, even if it is already selected. The up/down triangles below it scroll the list.
// Releasing either slider also plays the selected sound.
//
// Hardware: Hosyond 4" ESP32-S3 Viewer board with a speaker connected to the horn connector.
//

#define ARDUINO_HOSYOND_ESP32_S3_VIEWER

// Only compile in the font sizes this sketch uses, to save flash.
#define TEXT_SIZES_CUSTOM
#define TEXT_SIZE_2
#define TEXT_SIZE_3
#define TEXT_SIZE_4

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Viewer)."
#include "WrongBoard.h"
#endif

#include <Preferences.h>

#include "SerialX.h"
#include "Slider.h"

Arduino arduino;

constexpr auto TITLE = "Sound";

constexpr uint8_t TITLE_TEXT_SIZE = 4;
constexpr uint8_t LABEL_TEXT_SIZE = 2;

constexpr float NOTE_C5 = 523.25f;
constexpr uint16_t TEST_TONE_MILLIS = 300;

constexpr int16_t SLIDER_MARGIN = 20;
constexpr int16_t SLIDER_TOP = 50;
constexpr int16_t SLIDER_HEIGHT = 56;
constexpr int16_t SLIDER_SPACING = 86;

constexpr int16_t LIST_WIDTH = 170;
constexpr int16_t LIST_TOP = 0;
constexpr int16_t LIST_ROW_HEIGHT = 30;
constexpr int16_t LIST_GAP = 4;
constexpr int16_t LIST_ARROW_HEIGHT = 36;
constexpr uint8_t LIST_TEXT_SIZE = 2;

///
/// <summary>
/// Gets the width of the area to the left of the sound list, used by the sliders.
/// </summary>
/// <returns>Width in pixels</returns>
///
int16_t contentWidth()
{
   return arduino.width() - LIST_WIDTH;
}

constexpr int16_t CHECKBOX_SIZE = 24;
constexpr int16_t CHECKBOX_Y = SLIDER_TOP + 2 * SLIDER_SPACING;
constexpr int16_t CHECKBOX_TOUCH_PAD = 8;
constexpr int16_t CHECKBOX_TOUCH_WIDTH = 160;
constexpr float REPEAT_SECS = 4.0f;

float codecVolume = 180.0f;
Preferences preferences;
bool listPressed = false;
uint8_t listFirst = 0;
bool repeat = false;
bool checkboxPressed = false;
LGFX_Sprite listSprite(&arduino.display);
bool listSpriteCreated = false;
std::array<uint8_t, Sound::NUM_SOUNDS> sortedSounds;

///
/// <summary>
/// Draws the repeat checkbox and its label below the sliders.
/// </summary>
///
void drawCheckbox()
{
   const int16_t x = SLIDER_MARGIN;
   arduino.display.fillRect(x, CHECKBOX_Y, CHECKBOX_SIZE, CHECKBOX_SIZE, (uint16_t)Color::WHITE);
   arduino.display.fillRect(x + 3, CHECKBOX_Y + 3, CHECKBOX_SIZE - 6, CHECKBOX_SIZE - 6, (uint16_t)(repeat ? Color::BLUE : Color::BLACK));
   arduino.setTextSize(LABEL_TEXT_SIZE);
   arduino.display.setTextColor((uint16_t)Color::WHITE, (uint16_t)Color::BLACK);
   arduino.display.setCursor(x + CHECKBOX_SIZE + 8, CHECKBOX_Y + 4);
   arduino.display.print("Loop for 4s");
}

///
/// <summary>
/// Plays the selected sound, repeating it for up to REPEAT_SECS if the checkbox is checked.
/// </summary>
///
void playSelected()
{
   if (repeat)
   {
      arduino.sound.playNotificationAsyncFor(REPEAT_SECS);
   }
   else
   {
      arduino.sound.playNotificationAsync();
   }
}

///
/// <summary>
/// Gets the number of sound rows that fit in the list above the scroll arrows.
/// </summary>
/// <returns>Number of visible rows</returns>
///
uint8_t listVisibleRows()
{
   return (arduino.height() - LIST_TOP - LIST_ARROW_HEIGHT) / LIST_ROW_HEIGHT;
}

///
/// <summary>
/// Gets the height of each sound row, stretched so the rows and arrow buttons fill the display height exactly.
/// </summary>
/// <returns>Row height in pixels</returns>
///
int16_t listRowHeight()
{
   return (arduino.height() - LIST_TOP - LIST_ARROW_HEIGHT) / listVisibleRows();
}

///
/// <summary>
/// Gets the y coordinate (relative to the top of the list) where the scroll arrow buttons start.
/// </summary>
/// <returns>Arrow button top in pixels</returns>
///
int16_t listArrowY()
{
   return listVisibleRows() * listRowHeight();
}

///
/// <summary>
/// Draws the scrollable list of sounds and its up/down scroll buttons, highlighting the selected sound.
/// </summary>
///
void drawSelector()
{
   const int16_t x = 0;
   const int16_t width = LIST_WIDTH;
   const uint8_t visibleRows = listVisibleRows();
   const int16_t rowHeight = listRowHeight();
   const int16_t arrowY = listArrowY();
   const int16_t listHeight = arduino.height() - LIST_TOP;

   if (!listSpriteCreated)
   {
      arduino.createSprite(listSprite, LIST_WIDTH, listHeight, LIST_TEXT_SIZE);
      listSpriteCreated = true;
   }

   listSprite.fillScreen((uint16_t)Color::BLACK);
   for (uint8_t row = 0; row < visibleRows; row++)
   {
      const uint8_t i = listFirst + row;
      if (i >= Sound::NUM_SOUNDS)
      {
         break;
      }

      const int16_t y = row * rowHeight;
      const uint16_t fillColor = (uint16_t)(sortedSounds[i] == arduino.sound.soundIndex ? Color::BLUE : Color::DARKGRAY);
      const char* name = Sound::soundName(sortedSounds[i]);
      listSprite.fillRect(x, y, width, rowHeight - LIST_GAP, fillColor);
      listSprite.setTextColor((uint16_t)Color::WHITE, fillColor);
      listSprite.setCursor(
         x + (width - listSprite.textWidth(name)) / 2,
         y + (rowHeight - LIST_GAP - arduino.charH(LIST_TEXT_SIZE)) / 2);
      listSprite.print(name);
   }

   const int16_t halfWidth = width / 2;
   const int16_t arrowH = listHeight - arrowY;
   const bool canUp = listFirst > 0;
   const bool canDown = listFirst + visibleRows < Sound::NUM_SOUNDS;
   const uint16_t upColor = (uint16_t)(canUp ? Color::WHITE : Color::DARKGRAY);
   const uint16_t downColor = (uint16_t)(canDown ? Color::WHITE : Color::DARKGRAY);

   listSprite.fillRect(x, arrowY, halfWidth - LIST_GAP / 2, arrowH, (uint16_t)Color::DIMGRAY);
   listSprite.fillRect(x + halfWidth + LIST_GAP / 2, arrowY, halfWidth - LIST_GAP / 2, arrowH, (uint16_t)Color::DIMGRAY);

   const int16_t upCx = x + halfWidth / 2;
   const int16_t downCx = x + halfWidth + halfWidth / 2;
   const int16_t cy = arrowY + arrowH / 2;
   constexpr int16_t T = 10;
   listSprite.fillTriangle(upCx, cy - T, upCx - T, cy + T, upCx + T, cy + T, upColor);
   listSprite.fillTriangle(downCx, cy + T, downCx - T, cy - T, downCx + T, cy - T, downColor);
   listSprite.pushSprite(arduino.width() - LIST_WIDTH, LIST_TOP);
}

Slider volumeSlider(&arduino, "Volume", 2, "", 0.0f, 4.0f, &arduino.sound.volume, 0, SLIDER_TOP, contentWidth(), SLIDER_HEIGHT, SLIDER_MARGIN, LABEL_TEXT_SIZE);
Slider codecSlider(&arduino, "Codec", 0, "", 150.0f, 200.0f, &codecVolume, 0, SLIDER_TOP + SLIDER_SPACING, contentWidth(), SLIDER_HEIGHT, SLIDER_MARGIN, LABEL_TEXT_SIZE);

void setup()
{
   SerialX::begin();
   arduino.begin();

   for (uint8_t i = 0; i < Sound::NUM_SOUNDS; i++)
   {
      uint8_t j = i;
      while (j > 0 && strcasecmp(Sound::soundName(sortedSounds[j - 1]), Sound::soundName(i)) > 0)
      {
         sortedSounds[j] = sortedSounds[j - 1];
         j--;
      }
      sortedSounds[j] = i;
   }

   preferences.begin("Sound");
   arduino.sound.volume = preferences.getFloat("volume", 0.5f);
   codecVolume = preferences.getFloat("codec", codecVolume);
   arduino.sound.setDacVolume((uint8_t)codecVolume);

   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(TITLE_TEXT_SIZE);
   arduino.println(TITLE, Color::HEADING);

   volumeSlider.setWidth(contentWidth());
   codecSlider.setWidth(contentWidth());
   volumeSlider.draw();
   codecSlider.draw();
   drawCheckbox();
   drawSelector();
}

void loop()
{
   lgfx::touch_point_t touchPoint;
   const bool touched = arduino.display.getTouch(&touchPoint) > 0;

   const bool onCheckbox = touched &&
      touchPoint.y >= CHECKBOX_Y - CHECKBOX_TOUCH_PAD &&
      touchPoint.y < CHECKBOX_Y + CHECKBOX_SIZE + CHECKBOX_TOUCH_PAD &&
      touchPoint.x < SLIDER_MARGIN + CHECKBOX_TOUCH_WIDTH;
   if (onCheckbox && !checkboxPressed)
   {
      repeat = !repeat;
      drawCheckbox();
   }
   checkboxPressed = onCheckbox;

   const uint8_t visibleRows = listVisibleRows();
   const int16_t arrowY = LIST_TOP + listArrowY();
   const bool onList = touched &&
      touchPoint.x >= arduino.width() - LIST_WIDTH &&
      touchPoint.y >= LIST_TOP &&
      touchPoint.y < arduino.height();
   if (onList && !listPressed)
   {
      if (touchPoint.y >= arrowY)
      {
         const bool up = touchPoint.x < arduino.width() - LIST_WIDTH / 2;
         if (up && listFirst > 0)
         {
            listFirst--;
            drawSelector();
         }
         else if (!up && listFirst + visibleRows < Sound::NUM_SOUNDS)
         {
            listFirst++;
            drawSelector();
         }
      }
      else
      {
         const int16_t index = listFirst + (touchPoint.y - LIST_TOP) / listRowHeight();
         if (index < Sound::NUM_SOUNDS)
         {
            if (sortedSounds[index] != arduino.sound.soundIndex)
            {
               arduino.sound.soundIndex = sortedSounds[index];
               drawSelector();
            }
            playSelected();
         }
      }
   }
   listPressed = onList;

   if (volumeSlider.update(touched, touchPoint.x, touchPoint.y))
   {
      preferences.putFloat("volume", arduino.sound.volume);
      playSelected();
   }

   if (codecSlider.update(touched, touchPoint.x, touchPoint.y))
   {
      arduino.sound.setDacVolume((uint8_t)codecVolume);
      preferences.putFloat("codec", codecVolume);
      playSelected();
   }
}
