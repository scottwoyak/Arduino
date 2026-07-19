#include "ESP32_S3_Playground.h"
#include "SerialX.h"
#include "LGFXUtil.h"

ESP32_S3_Playground arduino;

Format posFormat("+###");
Format buttonFormat("########", Format::Alignment::LEFT);

void setup()
{
   SerialX::begin();
   arduino.begin();

   LGFXUtil::printLGFX(&arduino.display);
}

void displayHeader()
{
   arduino.setTextSize(4);
   arduino.println("Playground", Color::HEADING);
   arduino.setCursorY(arduino.getCursorY() + arduino.charH() / 4);
}

void displayEncoder(const char* label, Encoder& encoder)
{
   bool pressed = encoder.button.isPressed();
   arduino.print(label);
   arduino.print(pressed ? "Pressed" : "Released", buttonFormat, pressed ? Color::VALUE2 : Color::VALUE3);
   arduino.print("  ");
   arduino.println(encoder.getPosition(), posFormat);
}

void displayButton(const char* label, Button& button)
{
   bool pressed = button.isPressed();
   arduino.print(label);
   arduino.println(pressed ? "Pressed" : "Released", buttonFormat, pressed ? Color::VALUE2 : Color::VALUE3);
}

void loop()
{
   arduino.setCursor(0, 0);
   displayHeader();

   arduino.setTextSize(3);
   displayEncoder("Encoder A: ", arduino.encoderA);
   displayEncoder("Encoder B: ", arduino.encoderB);

   displayButton(" Button A: ", arduino.buttonA);
   displayButton(" Button B: ", arduino.buttonB);
}
