//
// Gate Publisher
//
// Reads the compass azimuth from an MLX90393 3-axis hall effect sensor and publishes
// live readings over a WebSocket telemetry connection, while also uploading a
// rolling-averaged CPU temperature reading to InfluxDB on a fixed interval.
//
// The site is always "Bragg", but there are two gate locations: Left and Right. See
// PublisherSketch.h for the shared init/loop sequence, site selection, and InfluxDB behavior.
//
// InfluxDB points uploaded (Measurement: Sensors, bucket=<selected>):
//
// - site=Bragg, location=<selected>, item=CPU
//     temperature: rolling average of cpuTemp.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//
// Note: azimuth is only streamed live over telemetry, not uploaded to InfluxDB.
//

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "LibraryVersion.h"
#include "MLX90393Magnetometer.h"
#include "WiFiSettings.h"

#include "PublisherSketch.h"

// This sketch's own version (e.g. "1.2"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("1.5");
constexpr auto SKETCH_NAME = "Gate_Publisher";

// ----------- InfluxDB site selection
constexpr InfluxContext INFLUX_PROMPTS[] = {
   { "Monitor", "Bragg", "Left" },
   { "Monitor", "Bragg", "Right" },
   { "Testing", "Bragg", "Left" },
   { "Testing", "Bragg", "Right" },
};

// ----------- Telemetry topic selection
constexpr const char* GATE_TELEMETRY_TOPICS[] = {
   "Gate/Left",
   "Gate/Right",
   "Test/Gate/Left",
   "Test/Gate/Right",
};

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring.
MLX90393Magnetometer magnetometer;

// Azimuth reading captured at startup, treated as the gate's zero (closed) angle.
float zeroAzimuth = 0.0f;

// True if the selected site is the left gate (rotates counter clockwise as it opens),
// false if it's the right gate (rotates clockwise as it opens).
bool leftGate = true;

// Last angle returned by gateAngle(), used as a deadband anchor so sensor noise doesn't
// jitter the published value.
float lastReportedAngle = 0.0f;

// Number of decimal places the angle is published with.
constexpr uint8_t ANGLE_DECIMALS = 1;

// Minimum change in angle required before lastReportedAngle is allowed to move. Derived
// from ANGLE_DECIMALS so it always matches the smallest step that can be published
// (e.g. 1 decimal -> 0.1 degrees).
constexpr float ANGLE_DEADBAND_DEGREES = []()
{
   float step = 1.0f;
   for (uint8_t i = 0; i < ANGLE_DECIMALS; i++)
   {
      step /= 10.0f;
   }
   return step;
}();

// While the gate is closed the angle is reported as 0 until it exceeds this angle, which
// hides sensor noise/drift at rest.
constexpr float GATE_MOVING_START_DEGREES = 3.0f;

// Once moving, the gate is considered closed again (reported as 0) when the angle falls
// below this angle. Lower than the start angle so the state doesn't chatter.
constexpr float GATE_MOVING_STOP_DEGREES = 1.5f;

// True once the angle has exceeded GATE_MOVING_START_DEGREES, until it falls back below
// GATE_MOVING_STOP_DEGREES.
bool gateMoving = false;

///
/// <summary>
/// Computes the gate's opening angle (0 to ~110 degrees) from the magnetometer's current
/// azimuth, relative to the azimuth captured at startup (zeroAzimuth). The left gate
/// rotates counter clockwise as it opens, and the right gate rotates clockwise, so the
/// sign of the azimuth delta is flipped for the left gate. A deadband around
/// lastReportedAngle prevents sensor noise from jittering the published value.
/// </summary>
/// <returns>Gate angle in degrees, with 0 meaning fully closed.</returns>
///
float gateAngle()
{
   magnetometer.read();

   float rawAzimuth = magnetometer.azimuth();
   float delta = rawAzimuth - zeroAzimuth;
   if (delta > 180.0f)
   {
      delta -= 360.0f;
   }
   else if (delta < -180.0f)
   {
      delta += 360.0f;
   }

   float angle = leftGate ? -delta : delta;

   // The gate should never report a negative angle (past fully closed). If it does,
   // treat the current position as the new zero (closed) angle instead.
   if (angle < 0.0f)
   {
      zeroAzimuth = rawAzimuth;
      angle = 0.0f;
   }

   if (!gateMoving)
   {
      if (angle <= GATE_MOVING_START_DEGREES)
      {
         lastReportedAngle = 0.0f;
         return lastReportedAngle;
      }

      gateMoving = true;
   }
   else if (angle < GATE_MOVING_STOP_DEGREES)
   {
      gateMoving = false;
      lastReportedAngle = 0.0f;
      return lastReportedAngle;
   }

   if (fabs(angle - lastReportedAngle) >= ANGLE_DEADBAND_DEGREES)
   {
      lastReportedAngle = angle;
   }

   return lastReportedAngle;
}

InfluxConfig INFLUX_CONFIG = {
   .prompts = INFLUX_PROMPTS,
   .includeEnclosureTemp = false,
   .includeCpuTemp = true,
};

TelemetryConfig TELEMETRY_CONFIG = {
   .prompts = GATE_TELEMETRY_TOPICS,
   .decimals = ANGLE_DECIMALS,
   .primary = TELEMETRY_RASPBERRY_ENDPOINT,
   .fallback = TELEMETRY_PRODUCTION_ENDPOINT,
   .deviceToken = TELEMETRY_DEVICE_TOKEN,
   .clientToken = TELEMETRY_CLIENT_TOKEN,
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = SKETCH_NAME,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

PublisherSketch sketch(PUBLISHER_CONFIG, INFLUX_CONFIG, TELEMETRY_CONFIG);

///
/// <summary>
/// Adds the most recent gate angle to a GetStatus reply, on top of
/// Logger's/SketchBase's base fields.
/// </summary>
/// <param name="status">The in-progress status to add fields to.</param>
///
void onStatus(LoggerStatus& status)
{
   status.add("Gate Angle", lastReportedAngle, 0);
}

void setup()
{
   sketch.addSensor("MLX90393", []() { return magnetometer.begin(); });
   sketch.setValueSource(gateAngle);

   sketch.begin();
   sketch.onStatus(onStatus);

   // Zero the gate angle to the azimuth measured at startup, and pick the rotation
   // direction based on the resolved site's location (Left vs Right).
   leftGate = strcmp(sketch.context().location, "Left") == 0;

   magnetometer.read();
   zeroAzimuth = magnetometer.azimuth();

   Logger.logInitializationComplete();
}

void loop()
{
   sketch.loop();
}
