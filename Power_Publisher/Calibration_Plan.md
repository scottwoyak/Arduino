# Power_Publisher & Woyak Power Sensor Calibration Plan

## Overview & Goal

Provide self-contained, Enphase-free calibration of CT current gain and phase offset from the Power_Publisher Settings screen. Calibration results are measurements for a CT type, not automatic per-channel settings: the user copies them into the sketch-local `CT_TYPE_TABLE`, then flashes the publisher sketch to make that type available.

AC line voltage remains calibrated per meter with `SetConfig vcal=<volts>`. CT gain and phase use a stable resistive load and known current through the CT (including turns through a line splitter):

1. **AC Line Voltage (`vScale`)** — calibrated per meter board with a True-RMS multimeter (`SetConfig vcal=<volts>`, already implemented).
2. **CT Current Gain (`ampsPerVolt`)** — calibrated per CT model (and optionally trimmed per channel) using a pure resistive test load on a **10× AC line splitter** + True-RMS multimeter/clamp meter.
3. **Voltage-to-CT Phase Shift (`phaseDeg`)** — decomposed into an additive **board phase** + **CT-model phase offset**:
	$$\phi_{\text{channel}} = \phi_{\text{board (9V AC wall-wart + ZMPT101B)}} + \Delta\phi_{\text{CT model}}$$
	calibrated using a pure tungsten resistive load (`300 W` heat lamp, or `2 × 100 W` heat lamps; $\text{PF} = 1.000$, true $Q = 0.0\text{ VAR}$) through a **10× AC line splitter**, so measured CT-type constants can be copied into each publisher sketch's preset table.

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

### 1. Sketch-local CT type table (`Power_Publisher.ino`)

Keep the CT type presets together in one clearly marked `CT_TYPE_TABLE` in each publisher sketch. Each row contains the token, display label, nominal rating, measured Amps-per-Volt scale, and phase offset from the reference CT. The Settings preset buttons and per-channel NVS identity are driven by this table.

The current rows are initial values, not bench-certified constants. In particular, replace the provisional `100R` phase offset after measurement. When adding a type, append one row and increase `NUM_AMP_OPTIONS`; its button then appears in Settings. Preserve existing row order because each channel stores the selected table index in NVS. Copy the same table row into any other power-publisher sketch that should offer that type.

`PowerSensorBase` stores the selected per-channel `phaseOffsetDeg`, and `MuxAdcPowerSensor` adds that offset to the board phase for the channel's power calculation. Keep CT offsets relative to the reference CT's measured total phase, not relative to a different CT model.

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
	* Accumulators (`double`): `sumIRmsV`, `sumLineV`, `sumUnitRawP`, `sumUnitRawQ`, plus `minIRmsV`, `maxIRmsV`, and failure reasons for missing reference, invalid input, and capture-start errors.
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
	* For the first/reference CT, save `totalPhaseDeg` as the reference board angle and use a `0.0f` CT offset.
	* For each subsequent CT type, compute `ctPhaseOffsetDeg = totalPhaseDeg - savedReferencePhaseDeg`.
	* If `knownTrueAmps > 0.0f`:
	  `calibratedAmpsPerVolt = knownTrueAmps / meanIRmsV`
	* Expose `startCalibrationCapture(size_t channel, float knownTrueAmps, uint16_t numSlices = 20)`, `isCalibrationCaptureActive() const`, `isCalibrationCaptureComplete() const`, and `consumeCalibrationResult(CalibrationResult* outResult)`.

---

### 3. CT Type Calibration Wizard (`Power_Publisher.ino`)

#### Settings workflow
* Add a `Calibrate CT Type` button to Settings. It calibrates one representative CT on the selected input; it does not change that channel's saved preset or automatically install a new type.
* Prompt for a simple type token, nominal rated amps, known RMS current through the conductor inside the CT, and whether this CT is the reference.
* The first calibration should use a known reference CT, preferably the square `SCT-013-100`. Capture and persist its measured total phase as the reference angle. Later type offsets are reported as `totalPhaseDeg - referencePhaseDeg`.
* Show preparation guidance before capture, then asynchronously capture 20 completed 50 ms slices while pinning the mux to the selected channel. Require valid line voltage, a connected CT, sufficient signal, and a steady load.
* Show sensor RMS volts, line voltage, calibrated `ampsPerVolt`, total phase, CT phase offset, and the sample spread. Provide a copyable initializer row for `CT_TYPE_TABLE`. For the reference CT, also show the measured `DEFAULT_PHASE_SHIFT_DEG` value and use a `0.0f` model offset.

#### Installing a calibrated CT type
1. Copy the reported row into the sketch-local `CT_TYPE_TABLE` in `Power_Publisher.ino`.
2. Increase `NUM_AMP_OPTIONS` so the Settings UI renders the new type button.
3. For the reference CT, update `MuxAdcPowerSensor::DEFAULT_PHASE_SHIFT_DEG` to the measured reference phase. The type table's phase values are offsets relative to that reference.
4. Reflash each Power_Publisher sketch that should offer the new preset. Then select the new type for each channel in Settings; the selected type identity, gain, and phase offset are stored per channel in NVS.

#### Runtime capture requirements
* Capture directly in the sampler at the end of complete trapezoidal slices; do not average repeatedly-read latched values from `loop()`.
* Keep `loop()` responsive: the Settings page polls completion and presents the result. A failed capture reports the reason and does not change NVS channel presets.
* Do not use the test CT's existing preset gain to calculate the result. Compute gain from known primary current divided by measured CT RMS volts, and derive phase from raw quadrature/in-phase covariance ratio.

---

## Step-by-Step Hardware Calibration Procedure

1. **Prepare the load and test channel.** Use an enclosed, properly rated tungsten resistive load (for example a 300 W heat lamp) on a properly rated 10× AC line splitter. Keep exposed energized conductors inaccessible. Select an open input channel in Settings and plug in the CT under test.
2. **Calibrate line voltage.** Measure AC voltage with a True-RMS meter and apply `SetConfig vcal=<measured volts>` before using the wizard.
3. **Measure current.** Use a True-RMS clamp meter or a safe series measurement to determine current through the conductor inside the CT. On a 10× splitter, enter the current seen by the CT, which is 10 times the lamp's 1× current (for example, 2.46 A at 1× becomes 24.60 A through the CT).
4. **Calibrate the reference CT first.** Turn on the load and wait 10–15 seconds for it to stabilize. In Settings, tap `Calibrate CT Type`, enter a token/rating/current, and answer yes to the reference question. Tap `Start capture` and record the displayed gain and total phase. Copy its table row with a `0.0f` phase offset and update `DEFAULT_PHASE_SHIFT_DEG` to the measured total phase.
5. **Calibrate every other CT type.** Keep the same board, load, and current measurement setup. Repeat the Settings wizard for each representative CT, answering no to the reference question. Record its reported `ampsPerVolt` and offset relative to the stored reference CT phase. The wizard rejects unplugged, low-signal, missing-voltage, or unsteady-load captures.
6. **Install and use the types.** Add each copied initializer to `CT_TYPE_TABLE`, increase `NUM_AMP_OPTIONS`, and flash each publisher sketch that should offer those types. In Settings, select the newly available CT button for each matching channel.

The `20A` CT may require a lower splitter multiplier/current to avoid core saturation. Use equipment and loads only within their ratings; do not handle energized wiring.
