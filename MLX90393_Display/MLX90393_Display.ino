//
// Reads the MLX90393 3-axis hall effect sensor and displays its XY angle and Z-axis
// angle. The Z-axis angle is 90 degrees when the field points directly above the
// sensor, and -90 degrees when it points directly behind (below) it.
//
// Two equal-sized squares on the right visualize the angles as white needles against
// a blue representation of the sensor: the top square shows a blue square outline
// (the sensor chip) with a needle from its center at the XY angle, and the bottom
// square shows a blue horizontal line (the sensor viewed edge-on) with a needle from
// its midpoint at the Z-axis angle. Each angle's numeric value is shown, unlabeled,
// to the left of its square, vertically aligned with it.
//
// Hardware: MLX90393 breakout wired over I2C (SDA/SCL/STEMMA QT).
//

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90393.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "DisplayBuffer.h"
#include "DisplayValue.h"
#include "SerialX.h"
#include "Timer.h"

// ----------- The Board
Arduino arduino;

// ----------- Text Sizes
constexpr uint8_t HEADER_SIZE = DEFAULT_HEADING_SIZE;

// ----------- Sensor
Adafruit_MLX90393 sensor;
constexpr uint16_t SAMPLE_PERIOD_MS = 33; // ~30 Hz
Timer sampleTimer(SAMPLE_PERIOD_MS);

// ----------- Content
float angleField = 0;
float zAngleField = 0;

// ----------- Angle needle squares (right side), with unlabeled value readouts to their left
constexpr uint8_t ANGLE_TEXT_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t NEEDLE_LAYER = 1;
constexpr uint8_t SENSOR_LAYER = 2;
Format angleValueFormat("+###.#", Format::Alignment::RIGHT);
DisplayValue angleValue(&arduino, angleValueFormat, ANGLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT);
DisplayValue zAngleValue(&arduino, angleValueFormat, ANGLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT);
DisplayBuffer angleBuffer;
DisplayBuffer zAngleBuffer;
int16_t squareSize;

void setup()
{
   SerialX::begin();

   Wire.begin();
   arduino.begin();

   if (!sensor.begin_I2C())
   {
      Serial.println("MLX90393 Not Found");
      while (1);
   }

   sensor.setGain(MLX90393_GAIN_1X);
   sensor.setResolution(MLX90393_X, MLX90393_RES_16);
   sensor.setResolution(MLX90393_Y, MLX90393_RES_16);
   sensor.setResolution(MLX90393_Z, MLX90393_RES_16);
   // OSR_0/FILTER_6 keeps the conversion time (13.36 ms, per the driver's tconv table)
   // plus its fixed 10 ms delay under the 33 ms (~30 Hz) sample period, using the
   // strongest filtering that still fits.
   sensor.setOversampling(MLX90393_OSR_0);
   sensor.setFilter(MLX90393_FILTER_6);

   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(HEADER_SIZE);
   arduino.println("MLX90393", Color::HEADING);

   int16_t top = arduino.getCursorY();
   int16_t bottom = (int16_t)arduino.height();
   int16_t rightRegionLeft = (int16_t)arduino.width() / 2;
   int16_t rightRegionWidth = (int16_t)arduino.width() - rightRegionLeft;

   // Value readouts sit just left of the squares, vertically centered on each one
   constexpr int16_t VALUE_GAP_PX = 10;
   int16_t valuesRight = rightRegionLeft - VALUE_GAP_PX;

   // The two squares stack vertically in the right half, sized to fit both within the
   // available height while staying no wider than the right half.
   squareSize = min(rightRegionWidth, (int16_t)((bottom - top) / 2));
   int16_t squaresLeft = rightRegionLeft + (rightRegionWidth - squareSize) / 2;

   angleValue.setPosition(valuesRight, top + squareSize / 2, VerticalAnchor::MIDDLE);
   zAngleValue.setPosition(valuesRight, top + squareSize + squareSize / 2, VerticalAnchor::MIDDLE);

   angleBuffer.bind(&arduino, squaresLeft, top, squareSize, squareSize);
   angleBuffer.setPaletteColor(NEEDLE_LAYER, Color::WHITE);
   angleBuffer.setPaletteColor(SENSOR_LAYER, Color::BLUE);

   zAngleBuffer.bind(&arduino, squaresLeft, top + squareSize, squareSize, squareSize);
   zAngleBuffer.setPaletteColor(NEEDLE_LAYER, Color::WHITE);
   zAngleBuffer.setPaletteColor(SENSOR_LAYER, Color::BLUE);

   arduino.display.drawRect(squaresLeft, top, squareSize, squareSize, (uint16_t)Color::GRAY);
   arduino.display.drawRect(squaresLeft, top + squareSize, squareSize, squareSize, (uint16_t)Color::GRAY);
}

void loop()
{
   if (sampleTimer.ready())
   {
      float x, y, z;
      if (sensor.readData(&x, &y, &z))
      {
         // Near-vertical fields make x/y small and noisy, so their raw atan2() angle
         // can flip by ~180 degrees between samples even when the magnet hasn't
         // physically moved (most noticeable when the Z-axis angle is near 0, i.e.
         // the field is mostly in the XY plane and its sign is unreliable). Rather
         // than trying to disambiguate via z's sign, continuity is used instead:
         // since the magnet is expected to move smoothly, whichever of the raw angle
         // or its 180-degree-flipped counterpart is closer to the previous reading is
         // kept, so a spurious flip snaps back to the physically consistent value.
         static float lastAngle = 0;
         float rawAngle = atan2(y, x) * RAD_TO_DEG;
         float flippedAngle = rawAngle + ((rawAngle < 0) ? 180.0f : -180.0f);
         angleField = (fabs(rawAngle - lastAngle) <= fabs(flippedAngle - lastAngle)) ? rawAngle : flippedAngle;
         lastAngle = angleField;

         zAngleField = atan2(z, sqrt(x * x + y * y)) * RAD_TO_DEG;

         angleValue.draw(angleField);
         zAngleValue.draw(zAngleField);

         // Top square: blue square outline represents the sensor chip, centered in the
         // square; the needle is a line from its center, pointing at the XY angle
         // (0 deg = up)
         int16_t centerX = squareSize / 2;
         int16_t centerY = squareSize / 2;
         float angleRad = angleField * DEG_TO_RAD;
         int16_t angleEndX = centerX + (int16_t)(centerX * sin(angleRad));
         int16_t angleEndY = centerY - (int16_t)(centerY * cos(angleRad));

         constexpr uint8_t SENSOR_SQUARE_MARGIN = 4;
         int16_t sensorLeft = SENSOR_SQUARE_MARGIN;
         int16_t sensorRight = squareSize - 1 - SENSOR_SQUARE_MARGIN;
         int16_t sensorTop = SENSOR_SQUARE_MARGIN;
         int16_t sensorBottom = squareSize - 1 - SENSOR_SQUARE_MARGIN;

         angleBuffer.clear();
         angleBuffer.drawLine(sensorLeft, sensorTop, sensorRight, sensorTop, SENSOR_LAYER);
         angleBuffer.drawLine(sensorRight, sensorTop, sensorRight, sensorBottom, SENSOR_LAYER);
         angleBuffer.drawLine(sensorRight, sensorBottom, sensorLeft, sensorBottom, SENSOR_LAYER);
         angleBuffer.drawLine(sensorLeft, sensorBottom, sensorLeft, sensorTop, SENSOR_LAYER);
         angleBuffer.drawLine(centerX, centerY, angleEndX, angleEndY, NEEDLE_LAYER);
         angleBuffer.draw();

         // Bottom square: blue horizontal line represents the sensor viewed edge-on,
         // centered vertically in the square; the needle starts at the blue line's
         // midpoint, pointing at the Z-axis angle (0 deg = right/horizontal, 90 deg =
         // up, -90 deg = down)
         int16_t sensorLineY = squareSize / 2;
         int16_t startX = squareSize / 2;
         int16_t startY = sensorLineY;
         float zAngleRad = zAngleField * DEG_TO_RAD;
         int16_t zAngleEndX = startX + (int16_t)(centerX * cos(zAngleRad));
         int16_t zAngleEndY = startY - (int16_t)(centerY * sin(zAngleRad));

         zAngleBuffer.clear();
         zAngleBuffer.drawLine(0, sensorLineY, squareSize - 1, sensorLineY, SENSOR_LAYER);
         zAngleBuffer.drawLine(startX, startY, zAngleEndX, zAngleEndY, NEEDLE_LAYER);
         zAngleBuffer.draw();
      }
   }
}
