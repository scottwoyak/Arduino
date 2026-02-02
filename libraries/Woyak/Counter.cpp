#include "Counter.h"
#include <Arduino.h>

Counter* Counter::_counters[Counter::MAX_COUNTERS] = {};
uint8_t Counter::_index = 0;
