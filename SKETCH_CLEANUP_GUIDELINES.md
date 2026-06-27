# Arduino Sketch Cleanup Guidelines

This document defines standards for cleaning up .ino sketches in the Arduino solution.

## File Structure Template

```cpp
/// <summary>
/// Brief description of what this sketch does.
/// </summary>
/// <remarks>
/// Optional longer description of the sketch's purpose, key features,
/// hardware setup, etc.
/// </remarks>

// System/standard library headers
#include <Arduino.h>
#include <string>

// Third-party library headers
#include <Adafruit_INA219.h>

// Local library headers (from libraries/Woyak)
#include "Feather.h"
#include "Status.h"
#include "TempSensor.h"
#include "Timer.h"

// Local project headers
#include "WiFiSettings.h"

constexpr auto VERSION = "v1.0";
constexpr auto SENSOR_COUNT = 5;
constexpr uint16_t BAUD_RATE = 115200;

Feather feather;
TempSensor sensor;

void setup()
{
	Serial.begin(BAUD_RATE);
	Wire.begin();

	feather.begin();
	sensor.begin();
}

void loop()
{
	// Main logic here
}

/// <summary>
/// Brief description of helper function.
/// </summary>
/// <param name="param1">Description of param1</param>
/// <returns>Description of return value</returns>
float helperFunction(float param1)
{
	return param1 * 2.0f;
}
```

## Cleanup Rules

### 1. Include Organization
- Group includes in order: system, third-party, local libraries, project headers
- Remove duplicate includes
- Remove unused includes
- Add missing includes for used types

### 2. Documentation
- Add file-level XML documentation (`/// <summary>` and `/// <remarks>`)
- Do NOT add XML docs for `setup()` or `loop()` functions (they're self-explanatory)
- Document all helper functions with params and returns
- Remove incorrect or obsolete comments
- Convert block comments (`/* */`) for functions to XML doc format (`///`)

### 3. Code Organization
- Organize as: INCLUDES, CONSTANTS, GLOBAL STATE, HELPERS, SETUP, LOOP (no section header comments)
- Keep related constants/variables together
- Declare globals close to where they're first used
- **Format objects**: Create them inline (e.g., `Format tempFormat("###.## F");`), do NOT break out format strings into separate `constexpr const char*` constants

### 4. Formatting
- Use 3 spaces for indentation (consistent with existing codebase)
- One blank line between function definitions
- Consistent brace placement (opening on same line for functions)
- Consistent spacing around operators

### 5. Code Quality
- Remove unused variables and function parameters
- Avoid variable shadowing (e.g., local `status` variable shadowing global `status` object)
- Replace magic numbers with named constants
- Rename cryptic variable names to be more descriptive

### 6. DRY Principles
- Extract repeated code blocks into helper functions
- Use constants for repeated values
- Consider reusable patterns from libraries/Woyak

## Common Issues to Fix

### Issue: Inconsistent Include Order
**Before:**
```cpp
#include <Arduino.h>
#include "Status.h"
#include <string>
#include "Feather.h"
```

**After:**
```cpp
#include <Arduino.h>
#include <string>

#include "Feather.h"
#include "Status.h"
```

### Issue: Unused Includes
**Before:**
```cpp
#include "RollingStats.h"  // Never used in sketch
#include "Influx.h"        // Only used once for WiFi
```

**After:**
```cpp
// Remove RollingStats.h entirely
// Keep Influx.h - still used
```

### Issue: No Documentation
**Before:**
```cpp
void setup()
{
	Serial.begin(115200);
}

float readValue() {
	return analogRead(A0) * 3.3 / 4096;
}
```

**After:**
```cpp
/// <summary>
/// Initializes serial communication and hardware.
/// </summary>
void setup()
{
	Serial.begin(115200);
}

/// <summary>
/// Reads and converts analog input to voltage.
/// </summary>
/// <returns>Voltage (0.0 - 3.3V)</returns>
float readValue()
{
	return analogRead(A0) * 3.3 / 4096;
}
```

### Issue: Commented-Out Code
**Before:**
```cpp
void loop()
{
	// float value = sensor.read();
	// Serial.println(value);

	// Old version - don't use
	// sensor.oldMethod();

	sensor.newMethod();
}
```

**After:**
```cpp
void loop()
{
	sensor.newMethod();
}
```

### Issue: Magic Numbers
**Before:**
```cpp
void readSensor() {
	for (int i = 0; i < 5; i++) {
		data[i] = analogRead(i);
		delay(50);
	}
}
```

**After:**
```cpp
constexpr uint8_t SENSOR_COUNT = 5;
constexpr uint16_t SENSOR_DELAY_MS = 50;

void readSensor()
{
	for (uint8_t i = 0; i < SENSOR_COUNT; i++)
	{
		data[i] = analogRead(i);
		delay(SENSOR_DELAY_MS);
	}
}
```

## Processing Order
1. Add file-level documentation
2. Reorganize includes
3. Extract constants
4. Remove dead code
5. Add/fix function documentation
6. Reformat for consistency
7. Final review

## Notes
- Preserve intentional educational code/comments
- Don't break existing functionality
- Test compile after each sketch batch
- Prioritize monitor sketches, then displays, then demos
