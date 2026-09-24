# Sketch Base Class Refactor Plan

Status: Not yet implemented. Saved for future implementation.

## Goal

Extract a shared boot-sequence base class used by both the InfluxDB-reporting
sketches (Monitor/Publisher) and the Viewer sketches, eliminating the
duplicated OTA-event-handler and logging boilerplate that exists today in
both `SketchBase` (libraries\Woyak\SketchBase.h) and `ViewerSketch`
(libraries\Woyak\ViewerSketch.h). Also rename the concrete sketch classes for
clarity now that a shared base exists.

## Resulting class hierarchy

```
SketchBase (new)                 — generic boot sequence
├── InfluxSketchBase (renamed from current SketchBase)
│   ├── MonitorSketch (renamed from Monitor)
│   └── PublisherSketch (renamed from Publisher)
└── ViewerSketch                 — telemetry subscription
```

## Step 1: Create the new `SketchBase` (shared boot base)

New file `libraries\Woyak\SketchBase.h`, extracted from the common pieces of
today's `SketchBase` and `ViewerSketch`:

- Members: `Arduino* _arduino`, `IStatus* _status`, `const char* _sketchName`,
  `const char* _version`, `bool _enableOTA`.
- `OTAUpdateEventHandler` private base implementation: `onUpdateAvailable()`,
  `onUpdateFailed()`, `onUpdateSucceeded()`, `onLogMessage()` (identical in
  both classes today).
- `_logMessage(const char*, LogSeverity)` / `_logMessage(const std::string&, LogSeverity)`
  helpers.
- `_printAndLog()` / `_printAndLogStatus()` helpers — moved here from today's
  `SketchBase`, so `ViewerSketch` can use them too instead of calling
  `_arduino->printlnInitStatus()` directly.
- A startup-print method (name TBD at implementation time, but must NOT use
  "Banner" in its name) that prints the sketch name/version as **two
  separate lines** ("Sketch... " / "Version... "), matching `ViewerSketch`'s
  current format. `InfluxSketchBase` will switch to this two-line format too
  (dropping its current single combined/truncated line).
- A connect method (mirroring today's `ViewerSketch::beginConnect()`): WiFi
  connect, conditional `enableOTA()`, Logger begin + `waitForClient()`.
- `checkForOTA()`.
- A base `loop()` step: `checkForOTA()` + `Logger.loop()` +
  `_arduino->updateStatusIndicators()` (subclasses call this first, then add
  their own per-loop work).
- `onStatus()` wrapper around `Logger.onStatus()`.

## Step 2: Rename current `SketchBase` → `InfluxSketchBase`

- Rename `libraries\Woyak\SketchBase.h` → `libraries\Woyak\InfluxSketchBase.h`.
- Rename the class `SketchBase` → `InfluxSketchBase`.
- Derive from the new `SketchBase`; remove now-duplicated members/methods
  (OTA handler overrides, `_logMessage`, `_printAndLog*`, `_arduino`/`_status`,
  banner/connect logic) and call into the new base instead.
- Keep `SketchConfig`, `SensorInit`, site resolution
  (`InfluxContextResolver`/`_site`), sensor registration/init, and the Influx
  post/flush cycle here.
- Switch its startup print to the new shared two-line method instead of its
  own single-line/truncated version.
- Update `begin()`/`loop()` to call the new base's connect/print/loop-step
  methods, then layer on site resolution, sensor init, and Influx-specific
  logic.

## Step 3: Rename `Publisher` → `PublisherSketch`

- Rename `libraries\Woyak\Publisher.h` → `libraries\Woyak\PublisherSketch.h`.
- Rename class `Publisher` → `PublisherSketch`, deriving from
  `InfluxSketchBase` (per Step 2's rename).
- Update constructor initializer list accordingly.
- Update every sketch that instantiates it — change `Publisher publisher(...)`
  → `PublisherSketch publisher(...)` (keep the local variable name
  `publisher`; only the type changes) in:
  - `Gate_Publisher\Gate_Publisher.ino`
  - `Wind_Publisher\Wind_Publisher.ino`
  - `Wave_Publisher\Wave_Publisher.ino`
  - `Telemetry_Publisher_Playground\Telemetry_Publisher_Playground.ino`
  - `Telemetry_Publisher_Display\Telemetry_Publisher_Display.ino`

## Step 4: Rename `Monitor` → `MonitorSketch`

- Rename `libraries\Woyak\Monitor.h` → `libraries\Woyak\MonitorSketch.h`.
- Rename class `Monitor` → `MonitorSketch`, deriving from `InfluxSketchBase`.
- Update constructor initializer list accordingly.
- Update every sketch that instantiates it — change `Monitor monitor(...)` →
  `MonitorSketch monitor(...)` (keep the local variable name `monitor`; only
  the type changes) in:
  - `Gate_Opener\Gate_Opener.ino`
  - `Lake_Temp_Monitor\Lake_Temp_Monitor.ino`
  - `Temp_Monitor_Display\Temp_Monitor_Display.ino`
  - `Temp_Monitor\Temp_Monitor.ino`

## Step 5: Update `ViewerSketch.h`

- Change `class ViewerSketch : private OTAUpdateEventHandler` → `class ViewerSketch : public SketchBase`.
- Remove now-duplicated members (`_arduino`, `_status`, banner/connect logic,
  OTA handler overrides, `_logMessage`, `_printAndLog*`).
- Keep topic resolution (`_topicResolver`, `resolveTopic()`) and telemetry
  subscription (`_client`, `beginTelemetry()`, `getClient()`).
- Update `loop()` to call the new base's loop step, then poll `_client`.
- Update `resolveTopic()` to use the shared `_printAndLogStatus()` helper
  (currently calls `_arduino->printlnInitStatus()` directly) for consistency.

## Step 6: Verify no references were missed

- Grep the whole workspace for any remaining bare `SketchBase`, `Publisher`,
  or `Monitor` type references (forward declarations, comments describing
  include order, pointer/reference parameters typed as `Publisher*`/
  `Monitor*`, etc.) to make sure nothing is missed. Update comments
  mentioning the old names too (e.g. header comments describing include
  order/requirements).

## Step 7: Build and test

- Build every affected project:
  - `MonitorSketch`-based sketches: `Gate_Opener`, `Lake_Temp_Monitor`,
	`Temp_Monitor_Display`, `Temp_Monitor`.
  - `PublisherSketch`-based sketches: `Gate_Publisher`, `Wind_Publisher`,
	`Wave_Publisher`, `Telemetry_Publisher_Playground`,
	`Telemetry_Publisher_Display`.
  - Viewer sketches: `Wind_Viewer`, `Gate_Viewer`, `Wave_Subscriber_Display`.
- Per project workflow rules, builds are not run automatically — compile
  each manually in Visual Micro and report back with any errors.

## Confirmed decisions

1. Startup print format: two separate lines ("Sketch... " / "Version... "),
   matching `ViewerSketch`'s current format. The method must not have
   "Banner" in its name.
2. `_printAndLog`/`_printAndLogStatus` helpers move to the new shared
   `SketchBase` so `ViewerSketch` can use them too.
3. Renamed file names: `InfluxSketchBase.h`, `PublisherSketch.h`,
   `MonitorSketch.h` (matching class name convention already used elsewhere,
   e.g. `SiteConfig.h`, `ViewerSketch.h`).
