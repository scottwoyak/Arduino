# Power Aggregation Middleware Hub — Requirements Specification

## 1. Overview & Purpose
Multiple ESP32-based `Consumption_Meter` devices measure AC power (Watts) across up to 8 split-core current transformer (CT) channels each and stream real-time readings to `TelemetryServer` over WebSockets.

A server-side **Power Aggregation Middleware Hub** (running alongside `TelemetryServer` on the server/Raspberry Pi, or integrated into it) is needed to:
1. Subscribe to all raw per-circuit power streams (`Power/<Location>/<Circuit Name>`).
2. Aggregate circuits that share the same `<Circuit Name>` (e.g., `"Lights"`, `"Kitchen"`) across one or more physical meters, plus compute an **Overall Total** across all active circuits.
3. Accumulate **Daily Energy (`Wh` / `kWh`)** per group and overall, resetting at local midnight.
4. Publish real-time grouped and total power/energy back to `TelemetryServer` so multiple `Power_Viewer` display devices remain 100% synchronized.
5. Periodically write aggregated and per-circuit power and energy metrics to **InfluxDB**.

---

## 2. TelemetryServer WebSocket Protocol Reference
The middleware communicates with `TelemetryServer` over WebSocket (`/ws`).

### 2.1 Handshake (`hello` -> `ack`)
Upon connecting, a client sends a JSON handshake message:

- **As a Subscriber (`role: "client"`):**
  ```json
  {
	"type": "hello",
	"role": "client",
	"deviceId": "power-hub-subscriber",
	"description": "PowerHub 1.0",
	"token": "<CLIENT_TOKEN>",
	"topics": []
  }
  ```
  *(Note: Passing `"topics": []` subscribes to all topics, or pass explicit topics / prefix patterns if supported by `TelemetryServer`. Dynamic topic updates after handshake can also be sent via `{ "type": "subscribe", "topics": [...] }`.)*

- **As a Publisher (`role: "device"`):**
  ```json
  {
	"type": "hello",
	"role": "device",
	"deviceId": "power-hub-publisher",
	"description": "PowerHub 1.0",
	"token": "<DEVICE_TOKEN>"
  }
  ```

- **Server Handshake Response:**
  ```json
  { "type": "ack" }
  ```
  *(Or `{ "type": "error", "message": "..." }` on failure.)*

### 2.2 Sample Messages (`type: "sample"`)
- **Incoming Sample Message (from `Consumption_Meter`):**
  ```json
  {
	"type": "sample",
	"topic": "Power/<Location>/<Circuit Name>",
	"value": 450,
	"dt": 200000,
	"decimals": 0
  }
  ```
  - `topic`: String identifying the power source.
  - `value`: Instantaneous apparent power in Watts (`double`).
  - `dt`: Microseconds elapsed since the publisher last sent a sample on this topic (`0` on first sample after connect).
  - `decimals`: Formatting precision (`0` for Watts).

- **Outgoing Sample Message (Published by Middleware Hub):**
  ```json
  {
	"type": "sample",
	"topic": "<OutputTopic>",
	"value": 1234.5,
	"dt": 200000,
	"decimals": 1
  }
  ```

---

## 3. Input Topic Schema (From `Consumption_Meter`)
Each `Consumption_Meter` has a device-level `Site` (e.g., `"Bragg"`) and `Location` (e.g., `"MainPanel"`, `"Subpanel"`, `"Garage"`), and each enabled CT channel has a configured `<Circuit Name>` (e.g., `"Lights"`, `"Kitchen"`, `"HVAC"`).

- **Topic Format:** `Power/<Location>/<Circuit Name>`
  - `<Location>`: The meter's configured device location (identifies which consumption meter / panel).
  - `<Circuit Name>`: The user-assigned circuit or group name on that meter.
- **Same-Meter Group Collision Handling:**
  - If a single meter has multiple channels assigned to the same logical group (e.g., two `"Lights"` circuits on `MainPanel`), the meter may append a channel disambiguator suffix `#<ch>` (e.g., `Power/MainPanel/Lights#1`, `Power/MainPanel/Lights#2`).
  - The middleware must treat `<Location>/<RawCircuitSegment>` (e.g., `MainPanel/Lights#1`) as the **unique physical circuit key**, while stripping any `#<n>` suffix to determine the **logical group name** (`<Circuit Name>`, e.g., `Lights`).
  - Similarly, `Power/MainPanel/Kitchen` and `Power/Subpanel/Kitchen` have distinct physical circuit keys (`MainPanel/Kitchen` and `Subpanel/Kitchen`) that both map to the logical group `"Kitchen"`.

---

## 4. Aggregation & Accumulation Requirements

### 4.1 Real-Time Power (Watts)
1. **Circuit State Table:** Maintain an in-memory state table of all active physical circuits keyed by `<Location>/<RawCircuitSegment>`, storing:
   - `location` (string)
   - `rawCircuitName` (string, with `#<n>` if present)
   - `groupName` (string, normalized `<Circuit Name>` with `#<n>` stripped)
   - `lastWatts` (double)
   - `lastSampleTime` (monotonic/UTC timestamp)
2. **Change-Only Publishing Behavior & Staleness:**
   - The Arduino `TelemetryClient` only transmits a WebSocket sample when the formatted value changes (or on initial connection). A steady load (e.g., `0 W` or a constant `120 W` lamp) may not emit new packets for minutes.
   - Do **not** zero out a circuit immediately just because a few seconds have elapsed without a packet.
   - Provide a configurable staleness/offline policy (or detect publisher WebSocket disconnect if integrated with `TelemetryServer` internals) so that if a meter disconnects completely, its last reading stops integrating after a bounded grace window (e.g., 5–10 minutes).
3. **Group Instantaneous Power:**
   - `GroupWatts[groupName] = sum(circuit.lastWatts)` across all active physical circuits where `circuit.groupName == groupName`.
4. **Overall Instantaneous Power:**
   - `TotalWatts = sum(circuit.lastWatts)` across all active physical circuits across all locations.

### 4.2 Daily Energy Accumulation (`Wh` / `kWh`)
1. **Continuous Time-Based Integration:**
   - Because incoming samples only arrive when Watts changes, the hub **must run a deterministic periodic integration tick** (e.g., every `1.0` second) in addition to (or instead of) event-driven `dt` integration:
	 - Every tick of `dtSeconds`, for each active circuit:
	   `deltaWh = circuit.lastWatts * (dtSeconds / 3600.0)`
	 - Add `deltaWh` to:
	   - `circuit.dailyWh`
	   - `group.dailyWh` (for `circuit.groupName`)
	   - `totalDailyWh`
2. **Persistence & Restart Recovery:**
   - Periodically persist the current day's state (local date `YYYY-MM-DD`, per-circuit `dailyWh` and `lastWatts`, per-group `dailyWh`, and `totalDailyWh`) to disk (e.g., atomic JSON file write or SQLite) every 30–60 seconds and on graceful shutdown (`SIGINT`/`SIGTERM`).
   - On startup, if the persisted state date matches today's local date, restore the accumulated `dailyWh` values so a service restart mid-day does not reset today's energy totals.
   - *(Optional fallback)* If no local state file exists for today on startup, query InfluxDB for the most recent `daily_wh` points from today (`start: today 00:00:00 local`) to seed the accumulators.
3. **Midnight Rollover:**
   - At local midnight (`00:00:00` in the server's local timezone), flush final end-of-day points to InfluxDB, reset all `dailyWh` accumulators (`circuit`, `group`, and `total`) to `0.0`, persist the new day's clean state, and publish the reset `0` daily totals to `TelemetryServer`.

---

## 5. Output Telemetry Schema (To Viewers via `TelemetryServer`)
To keep multiple `Power_Viewer` displays synchronized, the middleware publishes aggregated values back to `TelemetryServer`.

> **Loopback Prevention:** The middleware's subscriber filter must ignore its own output topics (only process raw `Power/<Location>/<Circuit Name>` topics; do not treat `PowerGroup/...` or `PowerTotal/...` as input circuits).

### 5.1 Published Topics
- **Per-Group Live Power (Watts):**
  - Topic: `PowerGroup/<Circuit Name>/Watts`
  - Value: Instantaneous summed Watts for `<Circuit Name>`, `decimals: 0`
- **Per-Group Daily Energy (Wh and/or kWh):**
  - Topic: `PowerGroup/<Circuit Name>/DailyWh` (`decimals: 0`) and/or `PowerGroup/<Circuit Name>/DailyKWh` (`decimals: 2`)
- **Overall Live Power (Watts):**
  - Topic: `PowerTotal/Watts`
  - Value: Instantaneous summed Watts across all circuits, `decimals: 0`
- **Overall Daily Energy (Wh and/or kWh):**
  - Topic: `PowerTotal/DailyWh` (`decimals: 0`) and/or `PowerTotal/DailyKWh` (`decimals: 2`)

### 5.2 Publish Pacing & Late-Joiner Synchronization
- **Real-Time Updates:** Whenever `GroupWatts[group]` or `TotalWatts` changes, publish the updated Watts topic (throttled to at most 5 Hz / 200 ms minimum interval per topic).
- **Energy Updates:** Publish updated `DailyWh` / `DailyKWh` topics whenever the rounded value changes (or once every few seconds).
- **Periodic Full Refresh (Sync for Newly Booted Viewers):**
  - Every **5–10 seconds**, re-publish the current value of all active `PowerGroup/*` and `PowerTotal/*` topics (or have `TelemetryServer` cache and replay the latest sample on each matching topic immediately when a new `client` subscribes).
  - This ensures that when any `Power_Viewer` boots up or reconnects, it immediately displays all groups, their live Watts, and their synchronized daily totals without waiting for loads to change.

---

## 6. InfluxDB Storage Requirements
Upload aggregated metrics to InfluxDB v2 on a fixed interval: **`UPLOAD_INTERVAL_S = 60` seconds**.

### 6.1 Time-Weighted Window Averaging
- Over each 60-second upload window, accumulate the time-weighted average Watts (`avg_watts`) for each circuit, each group, and the overall total (rather than taking an instantaneous snapshot at the 60-second boundary).
- Reset the 60-second window accumulators after each successful write.

### 6.2 InfluxDB Schema
- **Measurement:** `Power`
- **Upload Interval:** Every 60 seconds (plus a final flush just before local midnight reset).

1. **Overall Total Point:**
   - **Measurement:** `Power`
   - **Tags:**
	 - `site`: `<Site>` (e.g., `"Bragg"`)
	 - `circuit`: `"Total"`
	 - `type`: `"total"`
   - **Fields:**
	 - `watts`: 60s time-weighted average total power in W (`float`, 1 decimal)
	 - `daily_wh`: cumulative total energy today in Wh (`float`, 1 decimal)

2. **Per-Group Points (one point per logical `<Circuit Name>`):**
   - **Measurement:** `Power`
   - **Tags:**
	 - `site`: `<Site>` (e.g., `"Bragg"`)
	 - `circuit`: `<Circuit Name>` (e.g., `"Lights"`, `"Kitchen"`)
	 - `type`: `"group"`
   - **Fields:**
	 - `watts`: 60s time-weighted average group power in W (`float`, 1 decimal)
	 - `daily_wh`: cumulative group energy today in Wh (`float`, 1 decimal)

3. **Per-Circuit / Per-Meter Points (optional, configurable):**
   - **Measurement:** `Power`
   - **Tags:**
	 - `site`: `<Site>`
	 - `location`: `<Location>` (e.g., `"MainPanel"`, `"Subpanel"`)
	 - `circuit`: `<RawCircuitSegment>` (e.g., `"Lights#1"`)
	 - `group`: `<Circuit Name>` (e.g., `"Lights"`)
	 - `type`: `"circuit"`
   - **Fields:**
	 - `watts`: 60s time-weighted average circuit power in W (`float`, 1 decimal)
	 - `daily_wh`: cumulative circuit energy today in Wh (`float`, 1 decimal)

---

## 7. Companion Arduino Sketch Contracts (For Reference)
- **`Consumption_Meter.ino`:**
  - Resolves `site` and `location` at startup via `PublisherSketch` (`InfluxSketchBase`).
  - Publishes each configured CT channel (`1..8`) to `Power/<Location>/<Circuit Name>` (appending `#<ch>` if duplicate `<Circuit Name>` values exist on the same device).
- **`Power_Viewer.ino`:**
  - Uses `ViewerSketch` and `TelemetryClient` (`Role::CLIENT`) to subscribe to `PowerGroup/*` and `PowerTotal/*`.
  - Renders synchronized real-time Watts and Daily kWh totals across any number of viewer displays.
