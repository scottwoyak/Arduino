//
// Gate Publisher
//
// Reads the compass azimuth from a QMC5883P magnetometer (as found on HW-127/GY-273
// breakouts) and publishes live readings over a WebSocket telemetry connection, while
// also uploading a rolling-averaged CPU temperature reading to InfluxDB on a fixed
// interval.
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
#include "QMC5883PMagnometer.h"
#include "SerialTable.h"
#include "WiFiSettings.h"

#include "Publisher.h"

// ----------- Telemetry topic / InfluxDB location selection
constexpr SiteConfig GATE_LOCATIONS[] = {
   { "Gate/Left", "Monitor", "Bragg", "Left" },
   { "Gate/Right", "Monitor", "Bragg", "Right" },
   { "Test/Gate/Left", "Testing", "Bragg", "Left" },
   { "Test/Gate/Right", "Testing", "Bragg", "Right" },
};

// Uses WaveShare_ESP32_S3_Zero_Sensors's default I2C/RGB status LED/LED pins, which
// match this sketch's wiring.
Arduino arduino;
QMC5883PMagnometer magnetometer;

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

   if (fabs(angle - lastReportedAngle) >= (0.5f + ANGLE_DEADBAND_DEGREES))
   {
      lastReportedAngle = angle;

      // Temporary diagnostics: dump the raw x/y/z field components and resulting
      // angle whenever the reported angle changes, to help track down gate-angle
      // calibration issues. Remove once the angle math is validated.
      static constexpr SerialTable::Column DEBUG_COLUMNS[] = {
         { "X", 10, "###.##" },
         { "Y", 10, "###.##" },
         { "Z", 10, "###.##" },
         { "Angle", 10, "###.##" },
      };
      static SerialTable debugTable("Gate Angle Debug", DEBUG_COLUMNS);
      static bool headerPrinted = false;
      if (!headerPrinted)
      {
         debugTable.printHeader();
         headerPrinted = true;
      }
      debugTable.printRow(magnetometer.x(), magnetometer.y(), magnetometer.z(), angle);
   }

   return lastReportedAngle;
}

PublisherConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = SKETCH_NAME,
   .sites = GATE_LOCATIONS,
   .influxSensor = "Gate",
   .telemetryDecimals = 0,
   .includeEnclosureTemp = false,
   .includeCpuTemp = true,
   .enableOTA = true,
   .enableRebooter = true,
};

Publisher publisher(&arduino, PUBLISHER_CONFIG);

void setup()
{
   publisher.addSensor("QMC5883P", []() { return magnetometer.begin(); });
   // Temporarily disabled while debugging gate-angle calibration, so readings aren't
   // sent to the telemetry server; gateAngle() is instead polled directly from loop().
   //publisher.setValueSource(gateAngle);

   publisher.begin();

   // Zero the gate angle to the azimuth measured at startup, and pick the rotation
   // direction based on the resolved site's location (Left vs Right).
   leftGate = strcmp(publisher.site().influxLocation, "Left") == 0;

   magnetometer.read();
   zeroAzimuth = magnetometer.azimuth();
}

void loop()
{
   publisher.loop();

   // Temporary: poll gateAngle() directly so the debug table still prints while
   // publisher.setValueSource() is disabled above.
   gateAngle();
}
