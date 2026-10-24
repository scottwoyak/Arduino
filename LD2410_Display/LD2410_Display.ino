//
// HLK-LD2410C mmWave human presence radar display.
//
// Continuously reads target telemetry from an HLK-LD2410C 24 GHz mmWave radar sensor over
// UART (Serial1 on the board's RX/TX pins at 256000 baud) along with the digital OUT pin
// on GPIO 5. Displays "Moving: <N> cm" and "Still: <N> cm" readouts on the left, two
// vertical bar graphs on the right whose heights represent distance (0 - 600 cm) and whose
// colors represent signal energy (0 - 100%), and the digital OUT pin state and UART frame
// rate along the bottom.
//
// Hardware: Feather ESP32-S3 TFT with an HLK-LD2410C sensor wired to 5V (USB), GND,
// OUT -> GPIO 5, sensor TX -> Feather RX, and sensor RX -> Feather TX.
//

#include <Arduino.h>
#include <array>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#include "WrongBoard.h"
#endif

#include "Bar.h"
#include "ColorRange.h"
#include "DisplayValue.h"
#include "LD2410.h"
#include "RollingRate.h"
#include "SerialX.h"
#include "Table.h"

// ----------- Pins
constexpr uint8_t OUT_PIN = 5;

// ----------- The Board
Arduino arduino;

// ----------- Text Sizes
constexpr uint8_t HEADER_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t FOOTER_SIZE = 2;

// ----------- Sensor
constexpr uint16_t RATE_NUM_SAMPLES = 20;
constexpr float MAX_DISTANCE_CM = 600.0f;
LD2410 sensor(RX, TX, OUT_PIN);
RollingRate frameRate(RATE_NUM_SAMPLES);

// ----------- Display Items
constexpr int16_t BAR_WIDTH_PX = 30;
constexpr int16_t BAR_GAP_PX = 16;
constexpr int16_t BAR_RIGHT_MARGIN_PX = 6;
constexpr int16_t BAR_TOP_GAP_PX = 2;
constexpr int16_t BAR_BOTTOM_GAP_PX = 5;

Format outFormat("####");
Format rateFormat("####/s", Format::Alignment::RIGHT);

DisplayValue outValue(&arduino, outFormat, FOOTER_SIZE);
DisplayValue rateValue(
   &arduino,
   rateFormat,
   FOOTER_SIZE,
   DisplayValue::Alignment::RIGHT);

constexpr std::array<Table::ValueRow, 2> ROWS = {{
   { "Moving:", "### cm" },
   { "Still:", "### cm" },
}};
Table table(&arduino, 0, 0, ROWS);

ColorRange energyColorRange;
VerticalBar* movingBar = nullptr;
VerticalBar* stillBar = nullptr;

void setup()
{
   SerialX::begin();

   arduino.begin();
   sensor.begin();

   energyColorRange.addStop(0.0f, Color::DARKGRAY);
   energyColorRange.addStop(1.0f, Color::ORANGE);
   energyColorRange.addStop(100.0f, Color::YELLOW);

   // Heading on the top left
   arduino.clearDisplay();
   arduino.setTextSize(HEADER_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("LD2410C", Color::HEADING);

   int16_t headingHeight = arduino.charH(HEADER_SIZE);
   int16_t footerHeight = arduino.charH(FOOTER_SIZE);
   int16_t footerY = arduino.height() - footerHeight;

   // Place "Moving: <N> cm" and "Still: <N> cm" vertically centered on the left
   int16_t availableHeight = footerY - headingHeight;
   int16_t tableHeight = table.getRect().height;
   int16_t tableY = headingHeight + (availableHeight - tableHeight) / 2;
   table.setPosition(0, tableY);

   // Two vertical bars on the right: Moving (left bar) and Still (right bar)
   arduino.setTextSize(FOOTER_SIZE);
   int16_t labelHeight = arduino.charH(FOOTER_SIZE);
   int16_t stillFrameX = arduino.width() - BAR_RIGHT_MARGIN_PX - (BAR_WIDTH_PX + 2);
   int16_t movingFrameX = stillFrameX - BAR_GAP_PX - (BAR_WIDTH_PX + 2);
   int16_t frameY = labelHeight + BAR_TOP_GAP_PX;
   int16_t frameHeight = (footerY - BAR_BOTTOM_GAP_PX) - frameY;

   int16_t labelWidth = 3 * arduino.charW(FOOTER_SIZE);
   arduino.setCursor(movingFrameX + ((BAR_WIDTH_PX + 2) - labelWidth) / 2, 0);
   arduino.print("Mov", Color::LABEL);
   arduino.setCursor(stillFrameX + ((BAR_WIDTH_PX + 2) - labelWidth) / 2, 0);
   arduino.print("Stl", Color::LABEL);

   arduino.display.drawRect(movingFrameX, frameY, BAR_WIDTH_PX + 2, frameHeight, (uint16_t)Color::DARKGRAY);
   arduino.display.drawRect(stillFrameX, frameY, BAR_WIDTH_PX + 2, frameHeight, (uint16_t)Color::DARKGRAY);

   Rect16 movingBarRect(movingFrameX + 1, frameY + 1, BAR_WIDTH_PX, frameHeight - 2);
   Rect16 stillBarRect(stillFrameX + 1, frameY + 1, BAR_WIDTH_PX, frameHeight - 2);
   RangeF distanceRange(MAX_DISTANCE_CM, 0.0f);

   movingBar = new VerticalBar(movingBarRect, distanceRange, Color::DARKGRAY, Color::BLACK);
   stillBar = new VerticalBar(stillBarRect, distanceRange, Color::DARKGRAY, Color::BLACK);
   movingBar->clear();
   stillBar->clear();
   movingBar->draw(&arduino.display);
   stillBar->draw(&arduino.display);

   // Footer: digital OUT pin state on the lower left, UART frame rate on the lower right
   arduino.setCursor(0, footerY);
   arduino.print("OUT: ", Color::LABEL);
   outValue.setPosition(arduino.getCursorX(), footerY);
   rateValue.setPosition(arduino.width(), footerY);

   // Show initial placeholders before the first UART frame arrives
   table.setValueNone(0, Color::GRAY);
   table.setValueNone(1, Color::GRAY);
   table.draw();
}

void loop()
{
   // Always reflect the instantaneous digital OUT pin state on the display and onboard LED
   bool outHigh = sensor.isOutPinHigh();
   if (outHigh)
   {
      arduino.led.turnOn();
      outValue.draw("HIGH", Color::GREEN);
   }
   else
   {
      arduino.led.turnOff();
      outValue.draw("LOW", Color::GRAY);
   }

   if (!sensor.read())
   {
      return;
   }

   frameRate.tick();

   uint16_t movingDist = sensor.movingDistanceCm();
   uint8_t movingEnergy = sensor.movingEnergy();
   Color movingColor = energyColorRange.getColor(movingEnergy);

   uint16_t stillDist = sensor.stationaryDistanceCm();
   uint8_t stillEnergy = sensor.stationaryEnergy();
   Color stillColor = energyColorRange.getColor(stillEnergy);

   table.setValue(0, movingDist, movingColor);
   table.setValue(1, stillDist, stillColor);
   table.draw();

   movingBar->setColor(movingColor);
   movingBar->set((movingEnergy > 0 || movingDist > 0) ? movingDist : MAX_DISTANCE_CM);
   movingBar->draw(&arduino.display);

   stillBar->setColor(stillColor);
   stillBar->set((stillEnergy > 0 || stillDist > 0) ? stillDist : MAX_DISTANCE_CM);
   stillBar->draw(&arduino.display);

   rateValue.draw(frameRate.get(), Color::LIGHTGRAY);
}
