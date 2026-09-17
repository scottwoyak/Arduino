//
// Gate Publisher
//
// Reads the compass azimuth from an MLX90393 3-axis hall effect sensor and publishes
// live readings over a WebSocket telemetry connection, while also uploading a
// rolling-averaged CPU temperature reading to InfluxDB on a fixed interval.
//
// The site is always "Bragg", but there are two gate locations: Left and Right. See
// Publisher.h for the shared init/loop sequence, site selection, and InfluxDB behavior.
//
// InfluxDB points uploaded (Measurement: Sensors, bucket=<selected>):
//
// - site=Bragg, location=<selected>, sensor=Gate, item=CPU
//     temperature: rolling average of cpuTemp.readTemperatureF(), sampled every
//     SENSOR_INTERVAL_MS, over the last INFLUX_ROLLING_SAMPLES readings.
//
// Note: azimuth is only streamed live over telemetry, not uploaded to InfluxDB.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Gate_Publisher";

// This board is wired with a custom-powered I2C bus and an RGB LED status indicator.
#define ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS

#include "ArduinoBoard.h"
#include "MLX90393Magnetometer.h"
#include "WiFiSettings.h"

#include "Publisher.h"

// ----------- InfluxDB site selection
constexpr InfluxContext INFLUX_PROMPTS[] = {
   { "Monitor", "Bragg", "Left", "Gate" },
   { "Monitor", "Bragg", "Right", "Gate" },
   { "Testing", "Bragg", "Left", "Gate" },
   { "Testing", "Bragg", "Right", "Gate" },
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
Arduino arduino;
MLX90393Magnetometer magnetometer;

// Azimuth reading captured at startup, treated as the gate's zero (closed) angle.
float zeroAzimuth = 0.0f;

// True if the selected site is the left gate (rotates counter clockwise as it opens),
// false if it's the right gate (rotates clockwise as it opens).
bool leftGate = true;

// Last angle returned by gateAngle(), used as a deadband anchor to avoid publishing
// repeated whole-degree bounces when the raw angle dithers around a x.5 boundary.
float lastReportedAngle = 0.0f;

// Minimum change (beyond the 0.5 degree rounding boundary) required before
// lastReportedAngle is allowed to move, so noise near a x.5 boundary doesn't bounce
// the published whole-degree value back and forth.
constexpr float ANGLE_DEADBAND_DEGREES = 2.0f;

///
/// <summary>
/// Computes the gate's opening angle (0 to ~110 degrees) from the magnetometer's current
/// azimuth, relative to the azimuth captured at startup (zeroAzimuth). The left gate
/// rotates counter clockwise as it opens, and the right gate rotates clockwise, so the
/// sign of the azimuth delta is flipped for the left gate. A deadband around
/// lastReportedAngle prevents noise near a x.5 boundary from bouncing the published
/// whole-degree value back and forth.
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

   if (fabs(angle - lastReportedAngle) >= (0.5f + ANGLE_DEADBAND_DEGREES))
   {
      lastReportedAngle = angle;
   }

   return lastReportedAngle;
}

InfluxConfig INFLUX_CONFIG = {
   .prompts = INFLUX_PROMPTS,
};

TelemetryConfig TELEMETRY_CONFIG = {
   .prompts = GATE_TELEMETRY_TOPICS,
   .decimals = 0,
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = SKETCH_NAME,
   .influx = INFLUX_CONFIG,
   .telemetry = TELEMETRY_CONFIG,
   .includeEnclosureTemp = false,
   .includeCpuTemp = true,
   .enableOTA = true,
   .enableRebooter = true,
};

Publisher publisher(&arduino, PUBLISHER_CONFIG);

void setup()
{
   publisher.addSensor("MLX90393", []() { return magnetometer.begin(); });
   publisher.setValueSource(gateAngle);

   publisher.begin();

   // Zero the gate angle to the azimuth measured at startup, and pick the rotation
   // direction based on the resolved site's location (Left vs Right).
   leftGate = strcmp(publisher.site().location, "Left") == 0;

   magnetometer.read();
   zeroAzimuth = magnetometer.azimuth();
}

void loop()
{
   publisher.loop();
}
