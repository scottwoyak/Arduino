#include "Button.h"
#include <Arduino.h>

Button* Button::_buttons[Button::MAX_BUTTONS] = {};
uint8_t Button::_index = 0;
