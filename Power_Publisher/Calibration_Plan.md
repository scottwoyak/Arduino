# Power_Publisher & Woyak Power Sensor Calibration Plan

## Overview & Goal

Provide self-contained, Enphase-free calibration of all three real-power parameters ($P = V \cdot I \cdot \cos\theta$) across `libraries/Woyak` and `Power_Publisher`:

1. **AC Line Voltage (`vScale`)** — calibrated per meter board with a True-RMS multimeter (`SetConfig vcal=<volts>`, already implemented).
2. **CT Current Gain (`ampsPerVolt`)** — calibrated per CT model (and optionally trimmed per channel) using a pure resistive test load on a **10× AC line splitter** + True-RMS multimeter/clamp meter.
3. **Voltage-to-CT Phase Shift (`phaseDeg`)** — decomposed into an additive **board phase** + **CT-model phase offset**:
	$$\phi_{\text{channel}} = \phi_{\text{board (9V AC wall-wart + ZMPT101B)}} + \Delta\phi_{\text{CT model}}$$
	calibrated using a pure tungsten resistive load (`300 W` heat lamp, or `2 × 100 W` heat lamps; $\text{PF} = 1.000$, true $Q = 0.0\text{ VAR}$) through a **10× AC line splitter**, so calibrated CT-type constants can be baked directly into the shared library and reused across sketches and meter boards.

---

## Background & Root Cause Fixed by This Plan

* `Power_Publisher` currently maps `{ 20, 30, 100, 500 }` to `ampsPerVolt` only (`Power_Publisher.ino` lines 89–99) and applies a single board-wide `31.00°` phase angle (`MuxAdcPowerSensor::DEFAULT_PHASE_SHIFT_DEG`) on any meter without `Solar*` channels (`MuxAdcPowerSensor.h` line 902).
* On `Home`, channels `4` (`Panel2Black`) and `8` (`Panel2Red`) use **large round 100A split-core CTs**, whereas `Panel1Red`, `Panel1White`, `BarnRed`, and `BarnBlack` use **square YHDC `SCT-013-100` CTs**. The large round 100A CTs exhibit ~`8°–11°` more phase lag (`~41°–42°` total vs. `~33°` for square `SCT-013-100`), causing `-550 W` to `-600 W` under-reporting during `40 A` inductive heat-pump runs when locked to `31.00°`.
* Separating **board phase** from **CT-model phase offset** (and distinguishing `100` square vs. `100R` large round CTs) fixes this across all meters while remaining 100% compatible with nighttime solar auto-calibration on meters that have `Solar*` channels.

---

## Files to Update

1. `C:\SourceCode\Arduino\libraries\Woyak\CTPowerSensor.h` *(optional shared constants)*
2. `C:\SourceCode\Arduino\libraries\Woyak\PowerSensorBase.h`
3. `C:\SourceCode\Arduino\libraries\Woyak\MuxAdcPowerSensor.h`
4. `C:\SourceCode\Arduino\Power_Publisher\Power_Publisher.ino`

---

## Detailed Design

### 1. CT Model Preset Table (`PowerSensorBase.h`)

Define a first-class CT model enum and preset table in `PowerSensorBase` so every sketch shares both calibrated `ampsPerVolt` and `phaseOffsetDeg` per physical CT type:

```cpp
enum class CtModel : uint8_t
{
	CT_20A = 0,        // YHDC SCT006 20A:25mA + 20 ohm burden
	CT_30A = 1,        // YHDC SCT-013-030 (30A / 1V)
	CT_100A = 2,       // YHDC SCT-013-100 square (100A / 1V) — 0.00 deg reference CT
	CT_100A_ROUND = 3, // Large round 100A split-core CT
	CT_500A = 4,       // 500A:50mA + 47 ohm burden
	CUSTOM = 255       // Custom per-channel ampsPerVolt scale
};

struct CtModelSpec
{
	CtModel model;
	const char* token;       // CLI / GetConfig token: "20", "30", "100", "100R", "500"
	const char* displayName; // Touch UI button label: "20A", "30A", "100A", "100R", "500A"
	float ampsPerVolt;       // Calibrated Amps per 1.000V RMS
	float phaseOffsetDeg;    // Phase offset in deg relative to reference CT_100A (0.00 deg for CT_100A)
};
```

* Seed initial table values (to be updated with exact bench measurements once `Calibrate` is run on the 10× splitter):
  * `CT_20A`: `"20"`, `"20A"`, `CORE_40A` (`40.0f`), `0.0f`
  * `CT_30A`: `"30"`, `"30A"`, `CORE_30A` (`30.0f`), `0.0f`
  * `CT_100A`: `"100"`, `"100A"`, `CORE_100A` (`100.0f`), `0.0f` *(reference CT)*
  * `CT_100A_ROUND`: `"100R"`, `"100R"`, `CORE_100A` (`100.0f`), `+9.5f` *(initial estimate until bench-measured)*
  * `CT_500A`: `"500"`, `"500A"`, `CORE_213A` (`10000.0f / 47.0f`), `0.0f`
* Add helper lookup functions on `PowerSensorBase`:
  * `static const CtModelSpec* findCtModelByToken(const char* token)` (case-insensitive; also accept `"100S"`/`"100A"` as aliases for `"100"`, `"20A"` for `"20"`, `"30A"` for `"30"`, `"500A"` for `"500"`).
  * `static const CtModelSpec* findCtModel(CtModel model)`.
* Extend `PowerSensorBase::ChannelConfig` and `PowerSensorBase::ChannelState`:
  * Add `float phaseOffsetDeg = 0.0f;` to `ChannelConfig` and `ChannelState`.
  * Add `void setChannelPhaseOffsetDeg(size_t channel, float offsetDeg)` and `float channelPhaseOffsetDeg(size_t channel) const`.
* Update `PowerSensorBase::formatReadings()` (around lines 1710–1730):
  * Print `phaseShift=<deg> deg (<mode>)` for **all** connected channels whenever `hasPhaseCompensation()` is true (not only `ch.isSolar`).
  * Print `coreScale=<ampsPerVolt> A/V` (formatted to 2 decimal places, or integer when exact) instead of rounding `ampsPerVolt` to integer `coreRating`.
  * Note: `_deriveMissingHotLeg` already operates on phase-compensated `(avgPH, avgQH)` and `(avgPW, avgQW)` phasors, so derived channels (`Panel1Black` from `Panel1Red + Panel1White`) automatically benefit with no changes to KCL math.

---

### 2. Quadrature Phase Precedence & Multi-Slice Calibration Engine (`MuxAdcPowerSensor.h`)

#### A. Phase Modes & Effective Channel Angle
1. Extend `PhaseCalMode`:
	```cpp
	enum class PhaseCalMode : uint8_t
	{
		NOMINAL = 0,    // "nominal" (default board phase + CT model offset)
		NIGHT_AUTO = 1, // "auto"    (nighttime solar capacitive auto-calibration)
		MANUAL = 2      // "manual"  (bench/command-calibrated phase)
	};
	```
	Update `_phaseCalModeText(PhaseCalMode)` to return `"nominal"`, `"auto"`, or `"manual"`.
2. Expand `MAX_AUTO_PHASE_RAD` / valid manual phase bounds so larger round-CT total angles are never rejected:
	* Currently `MAX_AUTO_PHASE_RAD = 50.0f * DEG_TO_RAD_F`. Keep `50.0°` (or `55.0°`) for night solar auto-cal, and allow `[-60.0°, +65.0°]` for manual/bench calibration.
3. Compute effective per-channel phase `_effectiveChannelPhaseRad(size_t channel)` (used in `_onSampleTick` line 902 and `channelPhaseShiftDeg(size_t channel)` line 1312):
	* If `_solarCal[channel].calMode == PhaseCalMode::MANUAL` *(rename `_solarCal` to `_channelCal` for clarity)*:
	  return `_channelCal[channel].phaseRad`.
	* Else if `_channels[channel].isSolar && _channelCal[channel].calMode == PhaseCalMode::NIGHT_AUTO`:
	  return `_channelCal[channel].phaseRad`.
	* Else:
	  return `_boardPhaseShiftRad + (_channels[channel].phaseOffsetDeg * DEG_TO_RAD_F)`.
4. Update `channelPhaseCalibrationMode(size_t channel)`:
	* If `_channelCal[channel].calMode != PhaseCalMode::NOMINAL`, return `_phaseCalModeText(_channelCal[channel].calMode)`.
	* Else return `phaseCalibrationMode()` (the board mode: `"nominal"`, `"auto"`, or `"manual"`).
5. Update `_recomputeBoardPhaseShift()`:
	* If `_boardPhaseCalMode == PhaseCalMode::MANUAL`, **do not overwrite** `_boardPhaseShiftRad` from nighttime solar auto-calibration (only `resetAllAutoCalibration()` / `SetConfig phase=auto` returns board mode to `NOMINAL`/`NIGHT_AUTO`).
	* Otherwise, when one or more `isSolar` channels have `calMode == PhaseCalMode::NIGHT_AUTO`, compute:
	  ```cpp
	  sumRad += _channelCal[i].phaseRad - (_channels[i].phaseOffsetDeg * DEG_TO_RAD_F);
	  ```
	  so the learned board phase represents the board's `0.00°`-reference phase and non-solar channels (`20A`, `30A`, `100R`) accurately add their own CT model `phaseOffsetDeg`.
6. Add setters/restorers on `MuxAdcPowerSensor`:
	* `void setBoardPhaseShiftDeg(float phaseDeg, PhaseCalMode mode = PhaseCalMode::MANUAL)`
	* `void setChannelManualPhaseDeg(size_t channel, float phaseDeg)`
	* `void clearChannelManualPhase(size_t channel)`
	* Update `restoreBoardPhaseCalibration(float phaseDeg, PhaseCalMode mode)` and `restoreChannelAutoCalibration(size_t channel, float phaseDeg, PhaseCalMode mode, int8_t polaritySign)` so `PhaseCalMode::MANUAL` is accepted and restored for **any** channel `0..15` (not gated on `isSolar`).

#### B. Multi-Slice Calibration Capture Engine (Inside `_onSampleTick`)
Do **not** average `GetReadings` values from `loop()`, which would re-read the same latched slice across mux passes. Accumulate **completed 50 ms (3-cycle) trapezoidal slices** directly inside `MuxAdcPowerSensor`:

1. Add a lightweight capture state struct on `MuxAdcPowerSensor`:
	* `volatile bool _calCaptureActive = false;`
	* `volatile bool _calCaptureDone = false;`
	* `size_t _calTargetChannel = 0;`
	* `uint16_t _calTargetSlices = 20;` *(20 × 50 ms = 60 full 60 Hz cycles, ~1.1 s when parked on target channel)*
	* `uint16_t _calCollectedSlices = 0;`
	* Accumulators (`double`): `sumIRmsV`, `sumLineV`, `sumUnitRawP`, `sumUnitRawQ`, plus `minIRmsV`, `maxIRmsV`, and failure reason enum (`NONE`, `NO_VOLTAGE`, `UNPLUGGED`, `SIGNAL_TOO_LOW`, `UNSTEADY_LOAD`).
2. **Mux pinning during active capture (fast ~1.1 s capture)**:
	* While `_calCaptureActive` is true, `_advanceChannel()` keeps selecting `_calTargetChannel` (after normal slice latch) so all 20 slices complete in ~`1.1 seconds` even if 8 other CTs are plugged into the meter, then resumes normal round-robin immediately when `_calCollectedSlices == _calTargetSlices`.
3. **Per-slice accumulation & validation**:
	* Require `_voltageConnected && _lineVoltageV >= 80.0f` (else abort with `NO_VOLTAGE`).
	* Require `connected` (`_isCtBiasConnected(iDcVolts)`) (else abort with `UNPLUGGED`).
	* Accumulate `unitRawP = covVI * _voltageScale` and `unitRawQ = covVQI * _voltageScale` (Watts/VAR per `1.0 A/V`, independent of current `ampsPerVolt`).
	* Track `minIRmsV` and `maxIRmsV`.
	* After `_calTargetSlices` slices:
	  * Check `meanIRmsV >= 0.015f` (else `SIGNAL_TOO_LOW`).
	  * Check `(maxIRmsV - minIRmsV) / meanIRmsV <= 0.06f` (else `UNSTEADY_LOAD` — e.g., tungsten filament still warming up or load switched mid-capture).
4. **Exact calibration math on completion**:
	* For a pure resistive load ($\text{PF} = 1.000$, true $Q = 0$):
	  $$\phi_{\text{total,rad}} = \operatorname{atan}\!\left(\frac{\text{sumUnitRawQ}}{\text{sumUnitRawP}}\right)$$
	  *(Important: use `std::atan(sumUnitRawQ / sumUnitRawP)`, NOT `atan2`, so the result is in $(-90^\circ, +90^\circ)$. Flipping a CT clamp or clamping onto the opposite $180^\circ$ split-phase leg negates **both** `sumUnitRawP` and `sumUnitRawQ`, leaving their ratio identical—making calibration 100% invariant to CT orientation and L1/L2 leg.)*
	* `totalPhaseDeg = totalPhaseRad * RAD_TO_DEG_F`
	* `boardPhaseDeg = phaseShiftDeg()`
	* `ctPhaseOffsetDeg = totalPhaseDeg - boardPhaseDeg`
	* If `knownTrueAmps > 0.0f`:
	  `calibratedAmpsPerVolt = knownTrueAmps / meanIRmsV`
	* Expose `startCalibrationCapture(size_t channel, uint16_t numSlices = 20)`, `isCalibrationCaptureActive() const`, `isCalibrationCaptureComplete() const`, and `consumeCalibrationResult(CalibrationResult* outResult)`.

---

### 3. Commands, Persistence & Touch UI (`Power_Publisher.ino`)

#### A. NVS Persistence & Migration
* Add per-channel CT model array & NVS keys:
  ```cpp
  constexpr std::array<const char*, NUM_CT_INPUTS> PREF_KEY_CT_MODEL = {
	  "ctMod1", "ctMod2",  "ctMod3",  "ctMod4",  "ctMod5",  "ctMod6",  "ctMod7",  "ctMod8",
	  "ctMod9", "ctMod10", "ctMod11", "ctMod12", "ctMod13", "ctMod14", "ctMod15", "ctMod16",
  };
  ```
* **Non-destructive first-boot migration in `loadChannelConfig()`**:
  * If `ctMod<i>` exists in `Preferences`, load `CtModel` from `ctMod<i>`. If it is a standard preset (`!= CtModel::CUSTOM`), apply that preset's `ampsPerVolt` and `phaseOffsetDeg` (still honoring `amps<i>` when `ctMod<i> == CtModel::CUSTOM`).
  * If `ctMod<i>` does **not** exist yet (existing `Home` and `Garage` devices), infer `CtModel` from saved `amps<i>` (`~40.0 -> CT_20A`, `~30.0 -> CT_30A`, `~100.0 -> CT_100A`, `~212.8 -> CT_500A`, otherwise `CtModel::CUSTOM`).
* **Manual phase persistence**:
  * In `loadChannelConfig()` and `saveAutoCalibration()`, support `PhaseCalMode::MANUAL` (`2`) for `phMode` (`phBoard`) and for **all** 16 channels `phMod1..16` / `phDeg1..16` (currently non-solar channels have `phDeg<i>` / `phMod<i>` deleted in `saveAutoCalibration()`—preserve them whenever channel mode is `MANUAL`).

#### B. Custom `Calibrate` Command (`sketch.setCommandHandler`)
Register a sketch command `Calibrate` (alias `cal`) via `sketch.setCommandHandler(...)` in `setup()` alongside `sketch.onReadings(...)`:

* **Syntax**:
  `Calibrate <ch> [true_amps] [apply|board]`
  * `<ch>`: channel `1..16`.
  * `[true_amps]` *(optional)*: known effective primary current in Amps (e.g. `24.60` when `2.46 A` passes through a `10×` line splitter, or `2.46` on the `1×` window).
  * Mode flags *(optional)*:
	 * *(omitted — read-only report mode)*: runs a 20-slice capture (~1.1 s) withoutblocking `loop()` and replies via `DeviceServerClient::respond(...)` with the full measurement report so values can be copied into `CtModelSpec`:
		```text
		Calibrate ch16 (20 slices, V=121.8 V):
		  sensorRms     = 0.2451 V (spread 0.4%)
		  ampsPerVolt   = 100.37 A/V (at 24.60 A)
		  totalPhase    = 41.62 deg
		  boardPhase    = 32.85 deg (manual)
		  ctPhaseOffset = +8.77 deg
		  check @ cal   = 24.60 A, 2996.3 W, 0.0 VAR, PF=1.000
		```
	 * `board`: saves `boardPhase = totalPhase - channelCtPhaseOffset` (`PhaseCalMode::MANUAL`) to NVS (`phBoard`/`phMode`)—used with a reference `CT_100A` (`0.00°` offset) on the 10× splitter to lock the board's exact wall-wart + ZMPT101B phase. Also saves custom `ampsPerVolt` if `true_amps` was NOT passed? **No**—`board` only updates `boardPhase` so the channel stays on its `CT_100A` preset!
	 * `apply`: saves custom per-channel `MANUAL` phase (`totalPhase`) on `<ch>`, and if `true_amps` was supplied, also saves custom `ampsPerVolt` (`CtModel::CUSTOM`) on `<ch>`.
* **Async completion in `loop()`**:
  * Do not block `loop()` with `delay()`. Start capture in the command handler; in `loop()`, poll `powerSensor.isCalibrationCaptureComplete()`, format the output/apply NVS updates if requested, and call `DeviceServerClient::respond(...)`. Guard against concurrent `Calibrate` invocations while a capture is already running.

#### C. `SetConfig` & `GetConfig` Extensions (`trySetExtraConfig` / `getExtraConfigText`)
1. **`a1..a16` (`amps1..amps16`)**:
	* Accept preset tokens `"20"`, `"30"`, `"100"`, `"100R"`, `"500"` (case-insensitive) **or** any custom positive float `ampsPerVolt` (e.g. `SetConfig a4=100R` or `SetConfig a4=100.37`).
	* Selecting a standard preset sets `ctModels[i]`, updates `ctAmps[i]` and `phaseOffsetDeg`, and clears any per-channel `MANUAL` phase override on channel `i`.
	* Update interactive shell prompt `sendSingleChannelAmpsPrompt()` / `sendNextAmpsPrompt()` to accept `"20"`, `"30"`, `"100"`, `"100R"`, `"500"`, or a positive float.
2. **Direct phase & current calibration keys in `SetConfig`**:
	* `SetConfig phase=<deg>` — sets manual board phase in degrees (`PhaseCalMode::MANUAL`) and persists to NVS.
	* `SetConfig phase=auto` or `reset` — clears manual/auto board & channel phase overrides and returns to automatic/nominal mode (existing behavior).
	* `SetConfig phase=nominal` — resets board phase to `DEFAULT_PHASE_SHIFT_DEG` (`NOMINAL`).
	* `SetConfig ph<1..16>=<deg>|auto` — sets or clears a manual per-channel total phase override on channel `1..16`.
	* `SetConfig acal<1..16>=<measured_amps>` — immediately scales channel `<N>`'s `ampsPerVolt` from its live smoothed `sensorRmsVolts` (`measured_amps / sensorRmsVolts`, mirroring `vcal`) for quick in-panel clamp-meter trim.
3. **`GetConfig` output**:
	* For `a<N>`, output the preset token (`20`, `30`, `100`, `100R`, `500`) when `ctModels[i] != CtModel::CUSTOM`, or the custom `ampsPerVolt` to 2 decimals when `CUSTOM`.
	* Output `vScale=<val>` and `phase=<deg> deg (<mode>)`.
	* For any channel with `PhaseCalMode::MANUAL`, also output `ph<N>=<deg>`.

#### D. Touch Settings UI (`drawSettingsView` & `handleTouch`)
* Replace `NUM_AMP_OPTIONS = 4` / `AMP_GRID_COLS = 4` with the **5** standard `CtModelSpec` presets (`AMP_GRID_COLS = 5`, `AMP_GRID_ROWS = 1`):
  `20A` | `30A` | `100A` | `100R` | `500A`
* On the 480 px portrait screen (`contentW = 464 px`, `4 × 8 px` gaps), each of the 5 boxes is `86 px` wide—plenty of room for `"100R"` at `TABLE_TEXT_SIZE`.
* Highlight the active box by matching `ctModels[selectedSettingsChannel] == preset.model` (instead of comparing `ampsPerVolt`, since `100A` and `100R` both have ~`100 A/V`!).
* Tapping a preset box saves both `ctModels[selectedSettingsChannel]` and its `ampsPerVolt` + `phaseOffsetDeg`.

---

## Step-by-Step Hardware Calibration Procedure (Once Flashed)

1. **Set up the resistive test load**:
	* Plug a **300W incandescent/infrared tungsten heat bulb** (in a 300W-rated ceramic socket—or two 100W heat bulbs) into the **10× AC line splitter** with no other loads attached.
	* Turn the bulb ON and wait ~10–15 seconds for the tungsten filament resistance to stabilize.
2. **Calibrate AC voltage (`vScale`)**:
	* Measure AC voltage at the splitter's test jacks with a True-RMS multimeter (e.g., `121.8 V`) and run:
	  ```
	  SetConfig vcal=121.8
	  ```
3. **Measure true `1×` lamp current**:
	* Measure actual lamp current (`I_1x`) with a multimeter in series or a True-RMS clamp meter on the `1×` window (e.g., `2.46 A`, making the `10×` window **`24.60 A`**).
4. **Calibrate board reference phase using the square `100A` CT (`SCT-013-100`)**:
	* Set an open test channel (e.g., channel `16`) to `100`: `SetConfig a16=100`.
	* Clamp a square `SCT-013-100` around the **`10×` window** and plug it into channel `16`.
	* Lock the board's reference phase to `SCT-013-100` (`0.00°` offset):
	  ```
	  Calibrate 16 24.60 board
	  ```
	* Run `Calibrate 16 24.60` once more to confirm `ctPhaseOffset ≈ 0.00 deg`, `PF = 1.00`, `VAR ≈ 0.0`, and record `boardPhase` + `SCT-013-100`'s exact `ampsPerVolt`.
5. **Characterize each remaining CT model on channel `16`**:
	* **Large round `100A` CT (`100R`)** on the **`10×` window**:
	  `Calibrate 16 24.60` $\rightarrow$ record `ampsPerVolt` and `ctPhaseOffset` (expected ~`+8°` to `+11°`).
	* **`30A` CT (`SCT-013-030`)** on the **`10×` window**:
	  `Calibrate 16 24.60` $\rightarrow$ record `ampsPerVolt` and `ctPhaseOffset`.
	* **`500A` CT** *(if applicable)* on the **`10×` window**:
	  `Calibrate 16 24.60` $\rightarrow$ record `ampsPerVolt` and `ctPhaseOffset`.
	* **`20A` CT (`SCT006`)** on the **`1×` window** *(use `1×` = `2.46 A` so `25 A` does not saturate the `20A` core)*:
	  `Calibrate 16 2.46` $\rightarrow$ record `ampsPerVolt` and `ctPhaseOffset`.
6. **Commit constants & configure `Home`**:
	* Update `DEFAULT_PHASE_SHIFT_DEG` in `MuxAdcPowerSensor.h` and the 5 `CtModelSpec` `{ ampsPerVolt, phaseOffsetDeg }` entries in `PowerSensorBase.h` with the measured values.
	* Re-flash `Power_Publisher` onto `Home` (and `Garage` when desired).
	* On `Home`, set the two Panel 2 main CTs to `100R` (`SetConfig a4=100R`, `SetConfig a8=100R`) and calibrate `Home`'s AC voltage with a multimeter (`SetConfig vcal=<volts>`).
