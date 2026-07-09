# Copilot Instructions

## Environment & Workflow
- Before answering each user request, first read and follow the latest IDESTATE context, especially the current file, as authoritative. When the user issues a 'refresh' command, explicitly check the newly provided IDESTATE context to update awareness of the current active file and environment state.
- When determining which sketch a command applies to, always use the latest IDESTATE current file from the user's message as authoritative.
- Do not automatically run tests; only run tests when explicitly requested.
- Never attempt to build the solution or project automatically (do not use run_build or Visual Micro build commands). If a build or compilation is needed, explicitly ask the user to run it manually and provide the output if necessary.
- Do not run build/test commands that require VisualMicro context when operating outside Visual Studio VisualMicro pipeline; rely on user-run VisualMicro builds instead. User prefers not to run full solution builds in this workflow; ask them before any compile/run action and rely on user-provided serial output for runtime measurements.
- When asked to compile an Arduino sketch, build only the current sketch/project, not the entire solution.
- When creating new Arduino projects or sketches, do not add them to the Visual Studio solution file; user will manage solution entries manually.
- When creating new Arduino test sketches, only create the `.ino` file. Do not create `.vcxproj` project files or modify the `.slnx` solution file. The user imports new projects into the solution via Visual Micro themselves.
- Do not edit `.sln`/`.slnx` solution files (or other files) that are currently open in the IDE, per the active IDESTATE context; ask the user to close them first or make the change manually.
- When adding new test projects, always include them in `Tests/All_Tests/All_Tests.ino` so they run in the aggregate test project.
- When asked for a recommendation, offer solutions and ask the user what to do instead of automatically implementing one.

## General Coding Conventions
- Do not make changes in libraries outside of the Woyak library; third-party libraries should remain untouched.
- Formatting (indentation, brace placement/Allman style, empty-bodied function braces like `ArduinoWithDisplay() {}` in `libraries/Woyak/ArduinoWithDisplay.h`) is defined authoritatively in `.editorconfig` and enforced via Visual Studio's C++ formatting engine. Apply `.editorconfig` during cleanup passes instead of restating these values here.
- Start non-constant private and protected class members with underscores; private and protected constants should not start with underscores. Private functions should also start with underscores and be declared before public functions.
- Apply proper include bracket style: use quotes (`"..."`) for Woyak library headers and local project headers, and use angle brackets (`<...>`) for third-party/installed libraries like LovyanGFX, WiFi, and InfluxDbClient.
- Prefer keeping initialization code in `setup()` rather than extracting it into separate helper functions.
- Initialization precedence: (1) Prefer class-level member initialization for fixed defaults, (2) use constructor body assignment for runtime/config-dependent values, (3) use initializer lists only when required for correctness (e.g., references, const members, or members without default constructors). When no default is needed, leave the member declaration without an initializer.
- Prefer explicit, clearly listed overload variants over complex template metaprogramming (e.g., avoid `typename std::enable_if` unless templates are simple and clearly useful). User prefers to not use template functions.
- When defining string constants, prefer `const char*` to `const char VAR[]` (array style). Use pointer style for string literals.
- Use `constexpr` for all compile-time constants (scalar values, string literals, sizeof calculations). Reserve `const` only for runtime-initialized values or types that cannot be `constexpr` (like complex struct arrays). Maintain consistency throughout a file—avoid mixing `constexpr` and `const` for similar constant declarations.
- Prefer `x++` over `++x` for increment operations.
- Avoid calling `clearDisplay` every loop iteration in sketches because it causes visible screen flicker; only clear when needed (e.g., mode changes).
- Never use display text size 1 in sketches; use size 2 or larger for better readability. Only use text-size constants if that size is used in many places; otherwise, inline the size value.
- For numeric values, pass numeric types directly to print/println overloads (no manual String conversion); use a single `Format` rule that allows format patterns (e.g., `###/s`) or numeric width for alignment while keeping numeric inputs numeric. Only specify explicit `Format` length when it is actually needed; otherwise, rely on the format string to determine width. User prefers rate labels formatted as `"/s"` instead of `" per/s"` in display Format strings.
- In display updates, prefer using `Format` objects for values instead of ad-hoc string/print formatting.
- In display row rendering, prefer labeled `printlnR` overloads instead of building full concatenated row strings for numeric values; use default value colors unless a specific color is requested.
- For display header alignment, prefer explicit space-padded header strings (e.g., `"    Rate"`) instead of using a `Format` object.
- Use `micros` instead of `us` in names and identifiers.
- Always use the `Timer` class when waiting for something.
- Use `SPAN` or `SAMPLE_TIME` for timing-related names; prefer seconds-based timing variable names like `SAMPLE_PERIOD_S` instead of names using `WINDOWS` like `DISPLAY_WINDOW_SECONDS`.
- Prefer expressing timing as rate when value is >= 1 per second, but use interval constants when effective rate is < 1 per second.
- For time constants greater than 2 seconds, prefer using seconds-based constant names with an `_S` suffix (e.g., `NAME_S`) instead of milliseconds-based names with an `_MS` suffix.
- When addressing data quality issues, prefer root-cause stabilization logic over fixed sample-skipping heuristics.
- For mock-timer-based deterministic tests, prefer exact equality assertions (`assertEqual`) over range-style boolean checks when expected values are deterministic.
- Optimize code and suggestions for the ESP32-S3 platform, including using ESP32-specific APIs like `gpio_isr_handler_add()`, `esp_timer`, and `IRAM_ATTR` for ISR functions, and other ESP32-S3 features.
- Always format serial tables with the `SerialTable` class.
- Prefer using pointers instead of references for parameters/members where a choice exists.
- Prefer explicit-width integer types (uint8_t, uint16_t, uint32_t, uint64_t) over platform-dependent types like uint/unsigned int for new and cleaned-up code.
- For array "count" constants (e.g. an array named TARGET_SAMPLE_RATES), name the corresponding count constant with a NUM_ prefix followed by a plural noun (e.g. NUM_TARGET_SAMPLES) rather than an _COUNT suffix.

## Documentation Comment Style
- User prefers Visual Studio XML documentation comments (`/// <summary>`, `<param>`, `<returns>`) for class and method documentation. These are XML documentation comments used for IntelliSense.
- Wrap XML doc-comment blocks for classes, methods, properties, and enums in bare `///` lines: a blank `///` line before the block and a blank `///` line after it. Example:
  ```cpp
  ///
  /// <summary>
  /// Brief description here.
  /// </summary>
  ///
  class MyClass
  ```
- When documenting namespace functions and class static/member methods, always include complete parameter documentation with `<param>` tags and return value documentation with `<returns>` tags, even for simple inline functions. Example:
  ```cpp
  ///
  /// <summary>
  /// Convert C to F.
  /// </summary>
  /// <param name="celsius">Temperature in Celsius</param>
  /// <returns>Temperature in Fahrenheit</returns>
  ///
  ```
- A wrapped `/// <summary>` doc-comment block needs a blank line before it, separating it from the previous sibling declaration (e.g. between documented enum members), UNLESS it immediately follows a scope-opener (`{`), an access specifier (`public:`/`private:`/`protected:`), or a `// -----------` section-header comment.
- Do NOT add per-member doc comments to enum values whose names are self-evident (e.g. `PORTRAIT`, `LANDSCAPE`). Only add a single wrapped `///` XML summary comment on the enum declaration itself.
- For `.ino` sketch files, use a top-of-file `//` line-comment block (not XML `///` doc comments) describing what the sketch does, with blank `//` lines separating sections. Format as:
  ```cpp
  //
  // Brief description of what this sketch does.
  //
  // Optional longer description of the sketch's purpose, key features,
  // hardware setup, etc.
  //
  ```
- Do not add XML docs for `setup()` or `loop()` functions unless explicitly requested; they're self-explanatory.
- Keep section comments in the style `// ----------- COMMENT` where they already exist (e.g. grouping overloads by type in library headers); do not remove them during cleanup.

## Code Cleanup Requests
When the user asks for 'cleanup' (with no other file specified), apply it to the currently displayed/open file from IDE context (provided in the `IDESTATE CONTEXT`). A cleanup pass means bringing the file in line with all the conventions, plus the rules below. Always read `.editorconfig` as part of a cleanup pass and apply its settings (indentation, brace placement, empty-bodied function braces, etc.) as the authoritative source for formatting; this document intentionally defers to `.editorconfig` for those specifics rather than duplicating them, and focuses instead on naming, documentation, and code-organization conventions.

### Non-Negotiable Preservation Rules
- Preserve inline comments and commented-out code exactly as-is; do not remove or "clean up" commented-out code during a cleanup pass, even if it looks unused or obsolete.
- Don't break existing functionality.
- Do not automatically build or run tests as part of cleanup (see Environment & Workflow); ask the user to compile/test and report back if verification is needed.

### File Structure Template (.ino sketches)
```cpp
//
// Brief description of what this sketch does.
//
// Optional longer description of the sketch's purpose, key features,
// hardware setup, etc.
//

// System/standard library headers
#include <Arduino.h>
#include <string>

// Third-party library headers
#include <Adafruit_INA219.h>

// Local library headers (from libraries/Woyak)
#include "Feather_ESP32_S3.h"
#include "Status.h"
#include "TempSensor.h"
#include "Timer.h"

// Local project headers
#include "WiFiSettings.h"

constexpr auto VERSION = "v1.0";
constexpr auto SENSOR_COUNT = 5;
constexpr uint16_t BAUD_RATE = 115200;

Feather_ESP32_S3 feather;
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

///
/// <summary>
/// Brief description of helper function.
/// </summary>
/// <param name="param1">Description of param1</param>
/// <returns>Description of return value</returns>
///
float helperFunction(float param1)
{
   return param1 * 2.0f;
}
```

### Cleanup Rules

#### 1. Include Organization
- Group includes in order: system, third-party, local libraries, project headers.
- Remove duplicate includes.
- Remove unused includes.
- Add missing includes for used types.

#### 2. Documentation
- Add a file-level `//` line-comment header block (not XML doc comments) for non-testing sketches describing behavior, flow, outputs, and usage; separate sections with blank `//` lines. Do not use dashed separator blocks for this top-of-file block.
- Do NOT add XML docs for `setup()` or `loop()` functions unless explicitly requested.
- Document all helper functions with params and returns.
- Document all public methods (and constructors) in classes with params and usage where appropriate.
- Remove incorrect or obsolete comments (but never remove commented-out code; see Non-Negotiable Preservation Rules above).
- Convert block comments (`/* */`) for functions to XML doc format (`///`).
- Do NOT add per-member doc comments to enum values whose names are self-evident (e.g. `PORTRAIT`, `LANDSCAPE`); a single summary on the `enum` itself is sufficient.

#### 3. Code Organization
- For `.ino` sketches, organize as: INCLUDES, CONSTANTS, GLOBAL STATE, HELPERS, SETUP, LOOP — group code in this order without adding new banner/section-header comments for these top-level groups.
- Keep existing `// ----------- COMMENT` style section dividers within a file (e.g. overloads grouped by type in a library header); do not remove them.
- Keep related constants/variables together.
- Declare globals close to where they're first used.
- **Format objects**: Create them inline (e.g., `Format tempFormat("###.## F");`), do NOT break out format strings into separate `constexpr const char*` constants.

#### 4. Formatting
- Apply the formatting rules defined in `.editorconfig` (indentation, brace placement, empty-bodied function braces) as the authoritative source for a cleanup pass; read `.editorconfig` and match its settings rather than relying on values restated here.
- One blank line between function definitions (but no extra blank lines within a tightly grouped set of near-identical overloads unless separating distinct groups).
- Consistent spacing around operators.

#### 5. Code Quality
- Remove unused variables and function parameters.
- Avoid variable shadowing (e.g., local `status` variable shadowing global `status` object).
- Replace magic numbers with named constants.
- Rename cryptic variable names to be more descriptive.

#### 6. DRY Principles
- Extract repeated code blocks into helper functions.
- Use constants for repeated values.
- Consider reusable patterns from libraries/Woyak.

### Common Issues to Fix

#### Issue: Inconsistent Include Order
**Before:**
```cpp
#include <Arduino.h>
#include "Status.h"
#include <string>
#include "Feather_ESP32_S3.h"
```

**After:**
```cpp
#include <Arduino.h>
#include <string>

#include "Feather_ESP32_S3.h"
#include "Status.h"
```

#### Issue: Unused Includes
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

#### Issue: No Documentation
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
void setup()
{
   Serial.begin(115200);
}

///
/// <summary>
/// Reads and converts analog input to voltage.
/// </summary>
/// <returns>Voltage (0.0 - 3.3V)</returns>
///
float readValue()
{
   return analogRead(A0) * 3.3 / 4096;
}
```

#### Issue: Magic Numbers
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

#### Issue: Missing Wrapped `///` Doc-Comment Blocks
**Before:**
```cpp
/// <summary>Display rotation orientation options.</summary>
enum DisplayRotation
{
   /// <summary>Portrait orientation (0 degrees).</summary>
   PORTRAIT = 0,
   /// <summary>Landscape orientation (90 degrees).</summary>
   LANDSCAPE = 1,
   /// <summary>Portrait flipped orientation (180 degrees).</summary>
   PORTRAIT_FLIP = 2,
   /// <summary>Landscape flipped orientation (270 degrees).</summary>
   LANDSCAPE_FLIP = 3,
};
```

**After:**
```cpp
///
/// <summary>
/// Display rotation orientation options.
/// </summary>
///
enum DisplayRotation
{
   PORTRAIT = 0,
   LANDSCAPE = 1,
   PORTRAIT_FLIP = 2,
   LANDSCAPE_FLIP = 3,
};
```
Note: the enum-level summary uses the wrapped `///` block, but the individual members are self-evident and don't need their own doc comments.

### Processing Order
1. Add file-level documentation.
2. Reorganize includes.
3. Extract constants.
4. Add/fix function and class documentation.
5. Reformat for consistency.
6. Final review (verify no commented-out code or functionality was removed).

### Notes
- Preserve intentional educational code/comments.
- Don't break existing functionality.
- Ask the user to compile/test after each sketch batch rather than building automatically (see Environment & Workflow).
- When working through multiple files, prioritize monitor sketches, then displays, then demos.
