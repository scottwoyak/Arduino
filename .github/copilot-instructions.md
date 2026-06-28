# Copilot Instructions

## Project Guidelines
- User prefers Visual Studio XML documentation comments (/// <summary>, <param>, <returns>) for class and method documentation. When cleaning up code files, add Visual Studio style comments (///) before classes, methods, and properties. These are XML documentation comments used for IntelliSense. Example: /// <summary>Brief description</summary>
- When documenting namespace functions and class static methods with XML documentation comments, always include complete parameter documentation with <param> tags and return value documentation with <returns> tags, even for simple inline functions. This provides full IntelliSense support. Example: /// <summary>Convert C to F.</summary> /// <param name="celsius">Temperature in Celsius</param> /// <returns>Temperature in Fahrenheit</returns>
- When the user asks for 'cleanup', include Visual Studio-style XML documentation updates as part of the cleanup pass. Additionally, add Visual Studio style comments during code cleanup.
- When cleaning up non-testing sketches, always add a detailed description comment block at the top of the sketch.
- Do not automatically run tests; only run tests when explicitly requested.
- When cleaning up code, add IntelliSense-style XML documentation comments to constructors and public methods where appropriate, as well as to other methods where appropriate. Apply proper include bracket style: use quotes ("...") for Woyak library headers and local project headers, and use angle brackets (<...>) for third-party/installed libraries like LovyanGFX, WiFi, InfluxDbClient, Timer, etc.
- Prefer keeping initialization code in setup() rather than extracting it into separate helper functions.
- Prefer assigning values in constructor bodies instead of initializer lists by default, but use initializer lists when required for correctness or when members lack default constructors.
- Prefer class-level member initialization where possible instead of constructor initializer lists. When a default member initializer is not necessary, leave the member declaration without a default initializer.
- Do not make changes in libraries outside of the Woyak library; third-party libraries should remain untouched.
- Start non-constant private and protected class members with underscores; private and protected constants should not start with underscores.
- Use brace-on-new-line formatting style: place opening '{' on a new line.
- Avoid calling clearDisplay every loop iteration in sketches because it causes visible screen flicker; only clear when needed (e.g., mode changes).
- Never use display text size 1 in sketches; use size 2 or larger for better readability.
- Use "micros" instead of "us" in names and identifiers.
- Always use the Timer class when waiting for something.
- Prefer explicit, clearly listed overload variants over complex template metaprogramming (e.g., avoid typename std::enable_if unless templates are simple and clearly useful).
- Do not use WINDOW terminology for time durations; use alternatives like SAMPLE_TIME instead.
- In display row rendering, prefer using labeled printlnR overloads instead of building full concatenated row strings for numeric values; use default value colors unless a specific color is requested.
- When addressing data quality issues, prefer root-cause stabilization logic over fixed sample-skipping heuristics.

## Sketch Header Comments
- For sketch header comments, omit a separate sketch title line; use only summary lines between blank comment lines, and keep divider dashes ending at column 99.

## Arduino File Guidelines
- For .ino files, do not add comments on setup() and loop(); include a formatted top-of-file summary header only, using divider lines that end at column 99, a blank comment line before/after summary text, and no separate sketch title line.
- When creating new Arduino projects or sketches, do not add them to the Visual Studio solution file; user will manage solution entries manually.
- When creating new Arduino test sketches, only create the .ino file. Do not create .vcxproj project files or modify the .slnx solution file. The user imports new projects into the solution via Visual Micro themselves.
- When adding new test projects, always include them in Tests/All_Tests/All_Tests.ino so they run in the aggregate test project.
- Keep section comments in the style `// ----------- COMMENT` during cleanup rather than removing them.