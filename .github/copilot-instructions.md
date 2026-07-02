# Copilot Instructions

## Project Guidelines
- User prefers Visual Studio XML documentation comments (/// <summary>, <param>, <returns>) for class and method documentation. When cleaning up code files, add Visual Studio style comments (///) before classes, methods, and properties. These are XML documentation comments used for IntelliSense. Example: /// <summary>Brief description</summary>
- When documenting classes with XML comments, add an extra blank comment line (///) before and after the class documentation block. Example: /// \n/// <summary>Class description</summary>\n///\nclass MyClass
- When documenting namespace functions and class static methods with XML documentation comments, always include complete parameter documentation with <param> tags and return value documentation with <returns> tags, even for simple inline functions. This provides full IntelliSense support. Example: /// <summary>Convert C to F.</summary> /// <param name="celsius">Temperature in Celsius</param> /// <returns>Temperature in Fahrenheit</returns>
- When the user asks for 'cleanup', apply it to the currently displayed/open file from IDE context and include Visual Studio-style XML documentation updates as part of the cleanup pass. Preserve inline comments during cleanup; do not remove existing inline comments.
- Before answering each user request, first read and follow the latest IDESTATE context, especially the current file, as authoritative.
- When cleaning up non-testing sketches, add/expand a detailed top-of-file sketch comment block describing behavior, flow, outputs, and usage. Do not use dashed separator blocks.
- Do not automatically run tests; only run tests when explicitly requested.
- Do not run build/test commands that require VisualMicro context when operating outside Visual Studio VisualMicro pipeline; rely on user-run VisualMicro builds instead.
- When cleaning up code, add IntelliSense-style XML documentation comments to constructors and public methods where appropriate, as well as to other methods where appropriate. Apply proper include bracket style: use quotes ("...") for Woyak library headers and local project headers, and use angle brackets (<...>) for third-party/installed libraries like LovyanGFX, WiFi, and InfluxDbClient.
- Prefer keeping initialization code in setup() rather than extracting it into separate helper functions.
- Initialization precedence: (1) Prefer class-level member initialization for fixed defaults, (2) use constructor body assignment for runtime/config-dependent values, (3) use initializer lists only when required for correctness (e.g., references, const members, or members without default constructors). When no default is needed, leave the member declaration without an initializer.
- Do not make changes in libraries outside of the Woyak library; third-party libraries should remain untouched.
- Start non-constant private and protected class members with underscores; private and protected constants should not start with underscores. Private functions should also start with underscores and be declared before public functions.
- Use brace-on-new-line formatting style: place opening '{' on a new line.
- Avoid calling clearDisplay every loop iteration in sketches because it causes visible screen flicker; only clear when needed (e.g., mode changes).
- Never use display text size 1 in sketches; use size 2 or larger for better readability. Only use text-size constants if that size is used in many places; otherwise, inline the size value.
- For numeric values, pass numeric types directly to print/println overloads (no manual String conversion); use a single Format rule that allows format patterns (e.g., ###/s) or numeric width for alignment while keeping numeric inputs numeric.
- Use "micros" instead of "us" in names and identifiers.
- Always use the Timer class when waiting for something.
- Prefer explicit, clearly listed overload variants over complex template metaprogramming (e.g., avoid typename std::enable_if unless templates are simple and clearly useful).
- Use SPAN or SAMPLE_TIME for timing-related names; avoid the term WINDOW except for GUI concepts.
- In display row rendering, prefer using labeled printlnR overloads instead of building full concatenated row strings for numeric values; use default value colors unless a specific color is requested.
- For display header alignment in this sketch, prefer explicit space-padded header strings (e.g., "    Rate") instead of using a Format object.
- When addressing data quality issues, prefer root-cause stabilization logic over fixed sample-skipping heuristics.
- For mock-timer-based deterministic tests, prefer exact equality assertions (assertEqual) over range-style boolean checks when expected values are deterministic.
- When asked to compile an Arduino sketch, build only the current sketch/project, not the entire solution.
- Optimize code and suggestions for the ESP32-S3 platform, including using ESP32-specific APIs like gpio_isr_handler_add(), esp_timer, and IRAM_ATTR for ISR functions, and other ESP32-S3 features.
- When determining which sketch a command applies to, always use the latest IDESTATE current file from the user's message as authoritative.

## Arduino File Guidelines
- For .ino files, use regular // comments for sketch-level documentation at the top of the file. Format as: blank comment line, then summary line, blank comment line, then detailed description, and a final blank comment line at the bottom. Example:
//
// Summary line here.
//
// Detailed description here.
// Can span multiple lines.
// 
- For .ino files, do not add comments on setup() and loop() unless explicitly requested.
- When creating new Arduino projects or sketches, do not add them to the Visual Studio solution file; user will manage solution entries manually.
- When creating new Arduino test sketches, only create the .ino file. Do not create .vcxproj project files or modify the .slnx solution file. The user imports new projects into the solution via Visual Micro themselves.
- When adding new test projects, always include them in Tests/All_Tests/All_Tests.ino so they run in the aggregate test project.
- Keep section comments in the style `// ----------- COMMENT` during cleanup rather than removing them.
- Prefer helper print methods to be split into print and println variants instead of using an endLine boolean parameter.
- Include documentation comments when cleaning up utility code.
- Prefer x++ over ++x for increment operations.