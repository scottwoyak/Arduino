//
// Presence detector light-intensity simulator display.
//
// Uses an HLK-LD2410C 24 GHz mmWave radar sensor (UART on RX/TX at 256000 baud + digital
// OUT pin on GPIO 5) to drive a simulated room light displayed as a large filled square
// to the right of a two-line "Presence / Detector" heading.
//
// Light logic:
// - Wakes from OFF only when a Moving target is detected within MAX_LIGHT_DIST_CM with
//   energy >= TURN_ON_MOVING_ENERGY, preventing background room vibrations from turning
//   the light on in an empty room.
// - Stays ON while either Moving or Still presence (or the digital OUT pin) remains
//   active, plus a hold timer after presence clears.
// - Brightness scales inversely with distance (brighter when closer, dimmer when farther)
//   and is smoothed via a RollingAverage for gradual transitions.
//
// Hardware: Feather ESP32-S3 TFT with an HLK-LD2410C sensor wired to 5V (USB), GND,
// OUT -> GPIO 5, sensor TX -> Feather RX, and sensor RX -> Feather TX.
//

#include <Arduino.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#include "WrongBoard.h"
#endif

#include "ColorX.h"
#include "LD2410.h"
#include "RollingAverage.h"
#include "SerialX.h"
#include "Timer.h"

// ----------- Pins
constexpr uint8_t OUT_PIN = 5;

// ----------- The Board
Arduino arduino;

// ----------- Text Sizes
constexpr uint8_t HEADER_SIZE = DEFAULT_HEADING_SIZE;

// ----------- Presence / Light Parameters
constexpr uint16_t FULL_BRIGHT_DIST_CM = 60;
constexpr uint16_t MAX_LIGHT_DIST_CM = 400;
constexpr uint8_t TURN_ON_MOVING_ENERGY = 50;
constexpr uint8_t STILL_TRACK_ENERGY = 50;
constexpr float MIN_ON_BRIGHTNESS = 0.15f;
constexpr float HOLD_TIME_S = 5.0f;
constexpr uint8_t BRIGHTNESS_SMOOTH_SAMPLES = 8;

LD2410 sensor(RX, TX, OUT_PIN);
RollingAverage smoothedBrightness(BRIGHTNESS_SMOOTH_SAMPLES);
TimerSecs holdTimer(HOLD_TIME_S);

bool lightOn = false;
uint16_t activeDistanceCm = MAX_LIGHT_DIST_CM;

// ----------- Simulated Light Square Layout
constexpr int16_t HEADING_RIGHT_GAP_PX = 8;
constexpr int16_t SQUARE_MARGIN_PX = 4;
Rect16 lightSquareRect;
Color lastSquareColor = Color::BLACK;
bool hasDrawnSquare = false;

///
/// <summary>
/// Computes the instantaneous target brightness (0.0 - 1.0) from the latest radar state.
/// </summary>
/// <returns>Target brightness in the range 0.0 (off) to 1.0 (full brightness).</returns>
///
float computeTargetBrightness()
{
   LD2410::TargetState state = sensor.targetState();
   bool hasMoving = (state == LD2410::TargetState::MOVING) || (state == LD2410::TargetState::BOTH);
   bool hasStill = (state == LD2410::TargetState::STATIONARY) || (state == LD2410::TargetState::BOTH);

   uint16_t movingDist = sensor.movingDistanceCm();
   uint8_t movingEnergy = sensor.movingEnergy();
   uint16_t stillDist = sensor.stationaryDistanceCm();
   uint8_t stillEnergy = sensor.stationaryEnergy();

   if (!lightOn)
   {
      // Require a clear moving target within range to turn the light on initially
      if (hasMoving && movingEnergy >= TURN_ON_MOVING_ENERGY && movingDist > 0 && movingDist <= MAX_LIGHT_DIST_CM)
      {
         lightOn = true;
         activeDistanceCm = movingDist;
         holdTimer.reset();
      }
   }
   else
   {
      // Once on, keep the hold timer refreshed while any presence remains active
      bool presenceActive = sensor.isOutPinHigh() ||
                            (hasMoving && movingDist <= MAX_LIGHT_DIST_CM) ||
                            (hasStill && stillDist <= MAX_LIGHT_DIST_CM);
      if (presenceActive)
      {
         holdTimer.reset();
      }
      else if (holdTimer.ready())
      {
         lightOn = false;
      }

      // Track distance from Moving when moving, or from Still when strong; otherwise hold last distance
      if (hasMoving && movingDist > 0 && movingDist <= MAX_LIGHT_DIST_CM)
      {
         activeDistanceCm = movingDist;
      }
      else if (hasStill && stillEnergy >= STILL_TRACK_ENERGY && stillDist > 0 && stillDist <= MAX_LIGHT_DIST_CM)
      {
         activeDistanceCm = stillDist;
      }
   }

   if (!lightOn)
   {
      return 0.0f;
   }

   float closeness = constrain(
      static_cast<float>(MAX_LIGHT_DIST_CM - activeDistanceCm) /
         static_cast<float>(MAX_LIGHT_DIST_CM - FULL_BRIGHT_DIST_CM),
      0.0f,
      1.0f);

   return MIN_ON_BRIGHTNESS + closeness * (1.0f - MIN_ON_BRIGHTNESS);
}

void setup()
{
   SerialX::begin();

   arduino.begin();
   sensor.begin();

   // Draw two-line heading on the left: "Presence\nDetector"
   arduino.clearDisplay();
   arduino.setTextSize(HEADER_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("Presence", Color::HEADING);
   arduino.println("Detector", Color::HEADING);

   // Place the simulated light square to the right of the two-line heading
   int16_t headingWidth = 8 * arduino.charW(HEADER_SIZE);
   int16_t rightRegionLeft = headingWidth + HEADING_RIGHT_GAP_PX;
   int16_t rightRegionWidth = arduino.width() - rightRegionLeft;
   int16_t maxSquareWidth = rightRegionWidth - 2 * SQUARE_MARGIN_PX;
   int16_t maxSquareHeight = arduino.height() - 2 * SQUARE_MARGIN_PX;
   int16_t outerSize = min(maxSquareWidth, maxSquareHeight);

   int16_t outerX = rightRegionLeft + (rightRegionWidth - outerSize) / 2;
   int16_t outerY = (arduino.height() - outerSize) / 2;

   arduino.display.drawRect(outerX, outerY, outerSize, outerSize, (uint16_t)Color::DARKGRAY);
   lightSquareRect = Rect16(outerX + 1, outerY + 1, outerSize - 2, outerSize - 2);

   smoothedBrightness.set(0.0f);
}

void loop()
{
   if (!sensor.read())
   {
      return;
   }

   float targetBrightness = computeTargetBrightness();
   smoothedBrightness.set(targetBrightness);
   float brightness = smoothedBrightness.get();

   if (lightOn)
   {
      arduino.led.turnOn();
   }
   else
   {
      arduino.led.turnOff();
   }

   Color squareColor = Color565::blend(Color::BLACK, Color::YELLOW, brightness);
   if (!hasDrawnSquare || squareColor != lastSquareColor)
   {
      arduino.display.fillRect(
         lightSquareRect.x,
         lightSquareRect.y,
         lightSquareRect.width,
         lightSquareRect.height,
         (uint16_t)squareColor);
      lastSquareColor = squareColor;
      hasDrawnSquare = true;
   }
}
