//
// HLK-LD2410C mmWave human presence radar display.
//
// Continuously reads target telemetry from an HLK-LD2410C 24 GHz mmWave radar sensor over
// UART (Serial1 on the board's RX/TX pins at 256000 baud) along with the digital OUT pin
// on GPIO 5, and displays the presence state, moving/stationary/overall distances and
// signal energies, digital OUT pin state, and UART frame rate on the TFT display.
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
LD2410 sensor(RX, TX, OUT_PIN);
RollingRate frameRate(RATE_NUM_SAMPLES);

// ----------- Display Items
Format stateFormat("######", Format::Alignment::RIGHT);
Format outFormat("####");
Format rateFormat("####/s", Format::Alignment::RIGHT);

DisplayValue stateValue(
   &arduino,
   stateFormat,
   FOOTER_SIZE,
   DisplayValue::Alignment::RIGHT);
DisplayValue outValue(&arduino, outFormat, FOOTER_SIZE);
DisplayValue rateValue(
   &arduino,
   rateFormat,
   FOOTER_SIZE,
   DisplayValue::Alignment::RIGHT);

std::array columns = {
   Table::Column(""),
   Table::Column("Dist", "#### cm", Table::Alignment::RIGHT),
   Table::Column("Energy", "###%", Table::Alignment::RIGHT),
};
std::array rows = {
   Table::Row("Moving"),
   Table::Row("Still"),
   Table::Row("Detect"),
};
Table table(&arduino, 0, 0, columns, rows);

///
/// <summary>
/// Selects the display color corresponding to the current radar target state.
/// </summary>
/// <param name="state">The current LD2410 target state.</param>
/// <returns>Display color representing the target state.</returns>
///
Color targetStateColor(LD2410::TargetState state)
{
   switch (state)
   {
      case LD2410::TargetState::MOVING:
         return Color::ORANGE;
      case LD2410::TargetState::STATIONARY:
         return Color::YELLOW;
      case LD2410::TargetState::BOTH:
         return Color::GREEN;
      case LD2410::TargetState::NONE:
      default:
         return Color::GRAY;
   }
}

void setup()
{
   SerialX::begin();

   arduino.begin();
   sensor.begin();

   // Heading on the top left, with the target state badge on the top right
   arduino.clearDisplay();
   arduino.setTextSize(HEADER_SIZE);
   arduino.setCursor(0, 0);
   arduino.println("LD2410C", Color::HEADING);

   int16_t headingHeight = arduino.charH(HEADER_SIZE);
   int16_t footerHeight = arduino.charH(FOOTER_SIZE);
   int16_t footerY = arduino.height() - footerHeight;

   stateValue.setPosition(arduino.width(), headingHeight / 2, VerticalAnchor::MIDDLE);

   // Center the distance/energy table in the space between the heading and footer
   int16_t availableHeight = footerY - headingHeight;
   int16_t tableWidth = table.getWidth();
   int16_t tableHeight = table.getRect().height;
   int16_t tableX = (arduino.width() - tableWidth) / 2;
   int16_t tableY = headingHeight + (availableHeight - tableHeight) / 2;
   table.setPosition(tableX, tableY);

   // Footer: digital OUT pin state on the lower left, UART frame rate on the lower right
   arduino.setTextSize(FOOTER_SIZE);
   arduino.setCursor(0, footerY);
   arduino.print("OUT: ", Color::LABEL);
   outValue.setPosition(arduino.getCursorX(), footerY);
   rateValue.setPosition(arduino.width(), footerY);

   // Show initial placeholders before the first UART frame arrives
   stateValue.draw("NoUART", Color::RED);
   table.setValueNone(0, 0, Color::GRAY);
   table.setValueNone(0, 1, Color::GRAY);
   table.setValueNone(1, 0, Color::GRAY);
   table.setValueNone(1, 1, Color::GRAY);
   table.setValueNone(2, 0, Color::GRAY);
   table.setValue(2, 1, "", Color::BLACK);
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

   LD2410::TargetState state = sensor.targetState();
   stateValue.draw(sensor.targetStateText(), targetStateColor(state));

   bool hasMoving = (state == LD2410::TargetState::MOVING) || (state == LD2410::TargetState::BOTH);
   bool hasStationary = (state == LD2410::TargetState::STATIONARY) || (state == LD2410::TargetState::BOTH);
   bool hasTarget = sensor.isPresent();

   Color movingColor = hasMoving ? Color::VALUE : Color::GRAY;
   Color stationaryColor = hasStationary ? Color::VALUE : Color::GRAY;
   Color detectColor = hasTarget ? Color::VALUE : Color::GRAY;

   table.setValue(0, 0, sensor.movingDistanceCm(), movingColor);
   table.setValue(0, 1, sensor.movingEnergy(), movingColor);

   table.setValue(1, 0, sensor.stationaryDistanceCm(), stationaryColor);
   table.setValue(1, 1, sensor.stationaryEnergy(), stationaryColor);

   table.setValue(2, 0, sensor.detectionDistanceCm(), detectColor);
   table.setValue(2, 1, "", Color::BLACK);
   table.draw();

   rateValue.draw(frameRate.get(), Color::LIGHTGRAY);
}
