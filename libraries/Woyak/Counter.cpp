#include "Counter.h"

Counter* Counter::_counters[Counter::MAX_COUNTERS] = {};
uint8_t Counter::_index = 0;
