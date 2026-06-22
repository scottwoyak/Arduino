# Copilot Instructions

## Project Guidelines
- User prefers Visual Studio XML documentation comments (/// <summary>, <param>, <returns>) for class and method documentation. When cleaning up code files, add Visual Studio style comments (///) before classes, methods, and properties. These are XML documentation comments used for IntelliSense. Example: /// <summary>Brief description</summary>
- Do not automatically run tests; only run tests when explicitly requested.
- When cleaning up code, add IntelliSense-style XML documentation comments to constructors and public methods where appropriate, as well as to other methods where appropriate. Apply proper include bracket style: use quotes ("...") for Woyak library headers and local project headers, and use angle brackets (<...>) for third-party/installed libraries like LovyanGFX, WiFi, InfluxDbClient, Timer, etc.
- Prefer keeping initialization code in setup() rather than extracting it into separate helper functions.
- Do not make changes in libraries outside of the Woyak library; third-party libraries should remain untouched.

## Sketch Header Comments
- For sketch header comments, omit a separate sketch title line; use only summary lines between blank comment lines, and keep divider dashes ending at column 99.

## Arduino File Guidelines
- For .ino files, do not add comments on setup() and loop(); include a formatted top-of-file summary header only, using divider lines that end at column 99, a blank comment line before/after summary text, and no separate sketch title line.