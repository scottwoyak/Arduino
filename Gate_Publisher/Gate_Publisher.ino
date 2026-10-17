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

#include "ArduinoBoard.h"
#include "LibraryVersion.h"
#include "MLX90393Magnetometer.h"
#include "Preferences.h"
#include "RollingAverage.h"
#include "Timer.h"
#include "WiFiSettings.h"

#include "PublisherSketch.h"

// This sketch's own version (e.g. "1.2"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("1.7");
constexpr auto SKETCH_NAME = "Gate_Publisher";

// ----------- InfluxDB site selection
constexpr InfluxContext INFLUX_PROMPTS[] = {
   { "Telemetry_30_Day", "Bragg", "Left" },
   { "Telemetry_30_Day", "Bragg", "Right" },
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

// Number of decimal places the angle is published with.
constexpr uint8_t ANGLE_DECIMALS = 1;

// ----------- Zero (closed) azimuth persistence
// The gate's closed-position azimuth can drift slightly over time (e.g. magnetometer or
// mounting shifts), so it's continuously re-averaged whenever the gate is at rest and
// reporting (close to) a zero angle, and the averaged value is periodically saved to
// preferences. At startup, the saved value is used as the initial zeroAzimuth instead
// of whatever position the gate happens to be in, so a reboot while the gate is open
// doesn't re-zero it to the wrong angle.
constexpr const char* ZERO_AZIMUTH_KEY = "zeroAzimuth";
constexpr size_t ZERO_AZIMUTH_SAMPLES = 20;
constexpr float ZERO_ANGLE_CAPTURE_DEG = 1.0f;
constexpr uint32_t ZERO_AZIMUTH_SAVE_INTERVAL_S = 60;

RollingAverage zeroAzimuthAverage(ZERO_AZIMUTH_SAMPLES);
Timer zeroAzimuthSaveTimer(ZERO_AZIMUTH_SAVE_INTERVAL_S * 1000UL);
Preferences gatePreferences;

///
/// <summary>
/// Loads the previously saved zero (closed) azimuth from preferences, if any.
/// </summary>
/// <returns>The saved azimuth in degrees, or NAN if none has been saved yet.</returns>
///
float loadSavedZeroAzimuth()
{
   gatePreferences.begin(SKETCH_NAME, true);
   float saved = gatePreferences.isKey(ZERO_AZIMUTH_KEY) ? gatePreferences.getFloat(ZERO_AZIMUTH_KEY) : NAN;
   gatePreferences.end();
   return saved;
}

///
/// <summary>
/// Saves the given zero (closed) azimuth to preferences.
/// </summary>
/// <param name="azimuth">The azimuth in degrees to save.</param>
///
void saveZeroAzimuth(float azimuth)
{
   gatePreferences.begin(SKETCH_NAME, false);
   gatePreferences.putFloat(ZERO_AZIMUTH_KEY, azimuth);
   gatePreferences.end();
}

// ----------- Jitter suppression
// A resting gate jitters by up to ~0.6 degrees. At rest, a steady anchor angle is published.
// Once the angle moves PUBLISH_DEADBAND_DEG or more from the anchor, the gate is moving and
// every reading is published. It's at rest again after SETTLE_MS without a further move
// of that size. The Gate_Viewer needs a 2 degree rise to detect opening, so this doesn't
// delay detection.
constexpr float PUBLISH_DEADBAND_DEG = 1.0f;
constexpr uint32_t SETTLE_MS = 1000;

float anchorAngle = NAN;
bool moving = false;
Timer settleTimer(SETTLE_MS);

///
/// <summary>
/// Computes the gate's opening angle (0 to ~110 degrees) from the magnetometer's current
/// azimuth, relative to the azimuth captured at startup (zeroAzimuth). The left gate
/// rotates counter clockwise as it opens, and the right gate rotates clockwise, so the
/// sign of the azimuth delta is flipped for the left gate. The angle is reported as
/// measured; deciding whether the gate is open or closed is left to the viewer.
/// </summary>
/// <returns>Gate angle in degrees, never negative.</returns>
///
float rawGateAngle()
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

   // While the gate is at (or very near) its closed position, the current raw azimuth
   // is a fresh sample of the true zero (closed) angle, so fold it into the rolling
   // average that gets periodically saved for use as the starting zeroAzimuth on the
   // next boot.
   if (fabsf(angle) <= ZERO_ANGLE_CAPTURE_DEG)
   {
      zeroAzimuthAverage.set(rawAzimuth);
   }

   // The gate should never report a negative angle (past fully closed). If it does,
   // treat the current position as the new zero (closed) angle instead.
   if (angle < 0.0f)
   {
      zeroAzimuth = rawAzimuth;
      angle = 0.0f;
   }

   return angle;
}

///
/// <summary>
/// Returns the angle to publish: a steady anchor angle while at rest (so jitter isn't
/// published), or the live angle while the gate is moving.
/// </summary>
/// <returns>Gate angle in degrees.</returns>
///
float gateAngle()
{
   float angle = rawGateAngle();

   if (isnan(anchorAngle))
   {
      anchorAngle = angle;
   }

   if (fabsf(angle - anchorAngle) >= PUBLISH_DEADBAND_DEG)
   {
      anchorAngle = angle;
      moving = true;
      settleTimer.reset();
   }
   else if (moving && settleTimer.ready())
   {
      moving = false;
      anchorAngle = angle;
   }

   return moving ? angle : anchorAngle;
}

InfluxConfig INFLUX_CONFIG = {
   .prompts = INFLUX_PROMPTS,
   .includeEnclosureTemp = false,
   .includeCpuTemp = true,
};

TelemetryConfig TELEMETRY_CONFIG = {
   .prompts = GATE_TELEMETRY_TOPICS,
   .decimals = ANGLE_DECIMALS,
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
/// Formats the current gate angle, raw azimuth, zero azimuth, and motion state for the
/// "GetReadings" command registered via sketch.onReadings().
/// </summary>
/// <returns>Formatted readings text.</returns>
///
static std::string getReadings()
{
   float angle = rawGateAngle();
   float published = moving ? angle : (isnan(anchorAngle) ? angle : anchorAngle);
   std::ostringstream text;
   text.precision(1);
   text << std::fixed
        << "angle=" << published << " deg\n"
        << "rawAngle=" << angle << " deg\n"
        << "rawAzimuth=" << magnetometer.azimuth() << " deg\n"
        << "zeroAzimuth=" << zeroAzimuth << " deg\n"
        << "moving=" << (moving ? "true" : "false");
   return text.str();
}

///
/// <summary>
/// Commands registered with DeviceServerClient, listed by "Help" after the built-ins.
/// </summary>
///
static constexpr DeviceServerClient::CommandInfo GATE_PUBLISHER_COMMANDS[] = {
   { "Zero", "Captures the current magnetometer azimuth as the zero (closed) angle and persists it", {}, {}, true },
};

///
/// <summary>
/// Handles the "Zero" command registered via sketch.setCommandHandler().
/// </summary>
/// <param name="command">Full command text received from the DeviceServer.</param>
///
static void onGatePublisherCommand(const char* command)
{
   std::istringstream stream(command);
   std::string verb;
   stream >> verb;

   if (strcasecmp(verb.c_str(), "Zero") == 0)
   {
      magnetometer.read();
      zeroAzimuth = magnetometer.azimuth();
      zeroAzimuthAverage.reset();
      zeroAzimuthAverage.set(zeroAzimuth);
      anchorAngle = 0.0f;
      moving = false;
      saveZeroAzimuth(zeroAzimuth);

      std::ostringstream text;
      text.precision(1);
      text << std::fixed << "Zeroed gate at azimuth " << zeroAzimuth << " deg";
      DeviceServerClient::respond(text.str().c_str());
   }
   else
   {
      DeviceServerClient::respond((std::string("Unhandled command: ") + command).c_str());
   }
}

void setup()
{
   sketch.addSensor("MLX90393", []() { return magnetometer.begin(); });
   sketch.setValueSource(gateAngle);
   sketch.onReadings(getReadings, "Reports the current gate angle, raw azimuth, zero azimuth, and motion state");
   sketch.setCommandHandler(onGatePublisherCommand, GATE_PUBLISHER_COMMANDS, std::size(GATE_PUBLISHER_COMMANDS));

   sketch.begin();

   // Zero the gate angle to the saved zero (closed) azimuth, if one has been recorded
   // yet, otherwise fall back to the azimuth measured at startup. Pick the rotation
   // direction based on the resolved site's location (Left vs Right).
   leftGate = strcmp(sketch.context().location, "Left") == 0;

   magnetometer.read();
   float savedZeroAzimuth = loadSavedZeroAzimuth();
   zeroAzimuth = isnan(savedZeroAzimuth) ? magnetometer.azimuth() : savedZeroAzimuth;

   Logger.logInitializationComplete();
}

void loop()
{
   sketch.loop();

   if (zeroAzimuthSaveTimer.ready() && zeroAzimuthAverage.count() > 0)
   {
      saveZeroAzimuth(zeroAzimuthAverage.average());
   }
}
