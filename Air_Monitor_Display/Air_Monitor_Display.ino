//
// Air Monitor Display
//
// Reads particulate matter from a PMS5003 sensor, shows the current values in a table on
// the display, streams PM2.5 over a WebSocket telemetry connection, and uploads
// time-averaged readings to InfluxDB on a fixed interval. More sensors (e.g. CO2) will be
// added to this sketch later.
//
// Behavior:
// - Uses the shared PublisherSketch class (see PublisherSketch.h) to own the boot/init
//   sequence: display init, status LED, sensor init, WiFi, OTA, InfluxDB setup, and the
//   telemetry client.
// - This device's InfluxDB location is prompted for over Serial the first time it runs,
//   then saved to Preferences. Hold buttonA right after boot to force a re-prompt.
// - Continuously reads the PMS5003's serial stream and displays the latest PM1.0, PM2.5
//   and PM10 values.
//
// Outputs:
// - Display: table of the current PM1.0, PM2.5, and PM10 concentrations (ug/m3).
// - Telemetry: topics are item/site/location, published as PM1.0, PM2.5, PM10, CO2, VOC,
//   NOx, and AirQuality (worst 0-5 tier across all active readings, e.g. "AirQuality/site/location").
//
// Wiring:
// - PMS5003 TX -> board RX pin, PMS5003 RX -> board TX pin, 5V and GND.
//
// InfluxDB points uploaded (Measurement: Sensors):
//
// - site=<from Serial prompt/Preferences>, location=<from Serial prompt/Preferences>, item=PMS5003
//     pm1, pm25, pm10: time-averaged values over the INFLUX_INTERVAL_S upload interval.
//
// - site=<from Serial prompt/Preferences>, location=<from Serial prompt/Preferences>, item=CPU
//     temperature: the ESP32 CPU temperature at upload time.
//

#include "ArduinoBoard.h"

#if !defined(ADAFRUIT_FEATHER_M0) && !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
#error "This sketch is restricted to Feather boards (e.g. Feather ESP32-S3 TFT or Feather M0)."
#include "WrongBoard.h"
#endif

#include <NOxGasIndexAlgorithm.h>
#include <SensirionI2cScd4x.h>
#include <SensirionI2CSgp41.h>
#include <VOCGasIndexAlgorithm.h>
#include <Wire.h>

#include "AirQualityTiers.h"
#include "FieldTable.h"
#include "LibraryVersion.h"
#include "PMS5003.h"
#include "Timer.h"

#include "WiFiSettings.h"

#include "PublisherSketch.h"

// This sketch's own version (e.g. "1.0"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("1.1");
constexpr auto SKETCH_NAME = "Air_Monitor_Display";
constexpr auto PM1_TOPIC_PREFIX = "PM1.0";
constexpr auto PM25_TOPIC_PREFIX = "PM2.5";
constexpr auto PM10_TOPIC_PREFIX = "PM10";
constexpr auto CO2_TOPIC_PREFIX = "CO2";
constexpr auto VOC_TOPIC_PREFIX = "VOC";
constexpr auto NOX_TOPIC_PREFIX = "NOx";
constexpr auto AIR_QUALITY_TOPIC_PREFIX = "AirQuality";
constexpr uint16_t SGP41_CONDITIONING_S = 10;
constexpr size_t MAX_TOPIC_LENGTH = 64;
constexpr uint8_t TELEMETRY_DECIMAL_PLACES = 1;
constexpr uint8_t AIR_QUALITY_DECIMAL_PLACES = 3;
constexpr uint8_t TEXT_SIZE_SMALL = 2;
constexpr uint8_t STARTUP_DELAY_S = 5;
constexpr uint8_t INFLUX_INTERVAL_S = 60;
constexpr uint8_t INFLUX_DECIMAL_PLACES = 1;
constexpr uint8_t PMS_RX_PIN = RX;
constexpr uint8_t PMS_TX_PIN = TX;
constexpr auto PREFERENCES_NAMESPACE = "AirMonitor";
constexpr auto VOC_STATE0_PREF_KEY = "vocState0";
constexpr auto VOC_STATE1_PREF_KEY = "vocState1";
constexpr uint8_t VOC_CALIBRATION_M = 180;

// VOC algorithm tuning for a poorly ventilated 3D-printer room: keep the standard 100
// baseline offset, extend offset/gain learning from 12 h to 168 h (7 days) so sustained
// prints do not rapidly become the "normal" baseline, and raise gating to the maximum
// 3000 min (50 h) so elevated VOC events freeze baseline adaptation.
constexpr int32_t VOC_INDEX_OFFSET = 100;
constexpr int32_t VOC_LEARNING_TIME_OFFSET_H = 168;
constexpr int32_t VOC_LEARNING_TIME_GAIN_H = 168;
constexpr int32_t VOC_GATING_MAX_DURATION_M = 3000;
constexpr int32_t VOC_STD_INITIAL = 50;
constexpr int32_t VOC_GAIN_FACTOR = 230;

InfluxConfig INFLUX_CONFIG = {
   .promptForContext = true,
   .includeCpuTemp = true,
};

// Topic buffers, filled in once the site and location are known (see onSiteResolved()).
char pm1Topic[MAX_TOPIC_LENGTH] = "";
char pm25Topic[MAX_TOPIC_LENGTH] = "";
char pm10Topic[MAX_TOPIC_LENGTH] = "";
char co2Topic[MAX_TOPIC_LENGTH] = "";
char vocTopic[MAX_TOPIC_LENGTH] = "";
char noxTopic[MAX_TOPIC_LENGTH] = "";
char airQualityTopic[MAX_TOPIC_LENGTH] = "";

///
/// <summary>
/// Builds the telemetry topics as "item/site/location".
/// </summary>
/// <param name="site">The resolved InfluxDB site.</param>
///
void onSiteResolved(const InfluxContext& site)
{
   std::string suffix;
   if (site.site != nullptr && site.site[0] != '\0')
   {
      suffix += std::string("/") + site.site;
   }
   if (site.location != nullptr && site.location[0] != '\0')
   {
      suffix += std::string("/") + site.location;
   }
   strlcpy(pm1Topic, (std::string(PM1_TOPIC_PREFIX) + suffix).c_str(), sizeof(pm1Topic));
   strlcpy(pm25Topic, (std::string(PM25_TOPIC_PREFIX) + suffix).c_str(), sizeof(pm25Topic));
   strlcpy(pm10Topic, (std::string(PM10_TOPIC_PREFIX) + suffix).c_str(), sizeof(pm10Topic));
   strlcpy(co2Topic, (std::string(CO2_TOPIC_PREFIX) + suffix).c_str(), sizeof(co2Topic));
   strlcpy(vocTopic, (std::string(VOC_TOPIC_PREFIX) + suffix).c_str(), sizeof(vocTopic));
   strlcpy(noxTopic, (std::string(NOX_TOPIC_PREFIX) + suffix).c_str(), sizeof(noxTopic));
   strlcpy(airQualityTopic, (std::string(AIR_QUALITY_TOPIC_PREFIX) + suffix).c_str(), sizeof(airQualityTopic));
}

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = pm25Topic,
   .decimals = TELEMETRY_DECIMAL_PLACES,
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

PublisherSketch sketch(PUBLISHER_CONFIG, INFLUX_CONFIG, TELEMETRY_CONFIG);

PMS5003 pms;
SensirionI2cScd4x scd41;
SensirionI2CSgp41 sgp41;
VOCGasIndexAlgorithm vocAlgorithm;
NOxGasIndexAlgorithm noxAlgorithm;

InfluxField* co2Field = nullptr;
InfluxField* co2QualityField = nullptr;
InfluxField* scdTempField = nullptr;
InfluxField* humidityField = nullptr;
InfluxField* vocField = nullptr;
InfluxField* vocQualityField = nullptr;
InfluxField* noxField = nullptr;
InfluxField* noxQualityField = nullptr;

Timer sgpTimer(1000);
bool sgpConditioned = false;
uint16_t sgpConditioningCount = 0;

float co2 = NAN;
float tempF = NAN;
float humidity = NAN;
float vocIndex = NAN;
float noxIndex = NAN;
float lastTempC = 25.0f;
float lastHumidity = 50.0f;

InfluxField* pm1Field = nullptr;
InfluxField* pm1QualityField = nullptr;
InfluxField* pm25Field = nullptr;
InfluxField* pm25QualityField = nullptr;
InfluxField* pm10Field = nullptr;
InfluxField* pm10QualityField = nullptr;
InfluxField* airQualityField = nullptr;

Timer influxTimer(INFLUX_INTERVAL_S * 1000UL);
Timer vocCalibrationTimer(VOC_CALIBRATION_M * 60UL * 1000UL);
bool vocCalibrating = false;

// FieldTable reads these directly via pointer and only repaints a row's value when it changes.
float pm1 = NAN;
float pm25 = NAN;
float pm10 = NAN;

///
/// <summary>
/// Display-only table value backed by a caller-owned float, drawn in a color chosen from
/// its current value (e.g. green = good, red = bad).
/// </summary>
///
class ColoredValue : public ValueBase
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the ColoredValue class.
   /// </summary>
   /// <param name="format">Format pattern used to render the value.</param>
   /// <param name="value">Caller-owned variable holding the current value.</param>
   /// <param name="colorFor">Maps a value to the color it is drawn in.</param>
   ///
   ColoredValue(const char* format, const float* value, Color(*colorFor)(float))
      : ValueBase(format), _value(value), _colorFor(colorFor)
   {}

   std::string valueText() override
   {
      return _format.toString(*_value);
   }

   bool hasColor() const override
   {
      return true;
   }

   Color color() const override
   {
      return _colorFor(*_value);
   }

private:
   const float* _value;
   Color(*_colorFor)(float);
};

Color co2Color(float value)
{
   return AirQualityTiers::colorFor(value, AirQualityTiers::CO2_LIMITS);
}

Color vocColor(float value)
{
   return AirQualityTiers::colorFor(value, AirQualityTiers::VOC_LIMITS);
}

Color noxColor(float value)
{
   return AirQualityTiers::colorFor(value, AirQualityTiers::NOX_LIMITS);
}

// PM ratings use EPA AQI breakpoints (ug/m3).
Color pm1Color(float value)
{
   return AirQualityTiers::colorFor(value, AirQualityTiers::PM25_LIMITS);
}

Color pm25Color(float value)
{
   return AirQualityTiers::colorFor(value, AirQualityTiers::PM25_LIMITS);
}

Color pm10Color(float value)
{
   return AirQualityTiers::colorFor(value, AirQualityTiers::PM10_LIMITS);
}

ColoredValue pm1Value("###.# ug/m3", &pm1, pm1Color);
ColoredValue pm25Value("###.# ug/m3", &pm25, pm25Color);
ColoredValue pm10Value("###.# ug/m3", &pm10, pm10Color);
ColoredValue co2Value("#### ppm", &co2, co2Color);
ColoredValue vocValue("###", &vocIndex, vocColor);
ColoredValue noxValue("###.#", &noxIndex, noxColor);
FieldTable table(&sketch.arduino, 0, 0, TEXT_SIZE_SMALL);

///
/// <summary>
/// Computes the worst (highest) fractional air quality score (0.00-5.00) across all
/// active sensor readings, or NAN if no sensor has produced a valid reading yet.
/// </summary>
/// <returns>Worst fractional air quality score (0.00-5.00), or NAN if no readings are available.</returns>
///
float getWorstAirQualityScore()
{
   struct MetricRating
   {
      float value;
      const float* limits;
   };

   const MetricRating metrics[] = {
      { pm1, AirQualityTiers::PM25_LIMITS },
      { pm25, AirQualityTiers::PM25_LIMITS },
      { pm10, AirQualityTiers::PM10_LIMITS },
      { co2, AirQualityTiers::CO2_LIMITS },
      { vocIndex > 0 ? vocIndex : NAN, AirQualityTiers::VOC_LIMITS },
      { noxIndex > 0 ? noxIndex : NAN, AirQualityTiers::NOX_LIMITS },
   };

   float worstScore = NAN;
   for (const MetricRating& metric : metrics)
   {
      if (!isnan(metric.value))
      {
         float score = AirQualityTiers::scoreFor(metric.value, metric.limits);
         if (isnan(worstScore) || score > worstScore)
         {
            worstScore = score;
         }
      }
   }

   return worstScore;
}

///
/// <summary>
/// Computes the worst (highest) discrete air quality tier (0-5) across all active sensor
/// readings, or -1 if no sensor has produced a valid reading yet.
/// </summary>
/// <returns>Worst tier index (0-5), or -1 if no readings are available.</returns>
///
int8_t getWorstTier()
{
   struct MetricRating
   {
      float value;
      const float* limits;
   };

   const MetricRating metrics[] = {
      { pm1, AirQualityTiers::PM25_LIMITS },
      { pm25, AirQualityTiers::PM25_LIMITS },
      { pm10, AirQualityTiers::PM10_LIMITS },
      { co2, AirQualityTiers::CO2_LIMITS },
      { vocIndex > 0 ? vocIndex : NAN, AirQualityTiers::VOC_LIMITS },
      { noxIndex > 0 ? noxIndex : NAN, AirQualityTiers::NOX_LIMITS },
   };

   int8_t worstTier = -1;
   for (const MetricRating& metric : metrics)
   {
      if (!isnan(metric.value))
      {
         int8_t tier = AirQualityTiers::tierFor(metric.value, metric.limits);
         if (tier > worstTier)
         {
            worstTier = tier;
         }
      }
   }

   return worstTier;
}

///
/// <summary>
/// Records the current worst fractional air quality score (0.00-5.00) for InfluxDB and
/// publishes it over telemetry if at least one sensor reading is available.
/// </summary>
///
void publishAirQuality()
{
   float worstScore = getWorstAirQualityScore();
   if (!isnan(worstScore))
   {
      airQualityField->set(worstScore);
      sketch.publish(airQualityTopic, worstScore, AIR_QUALITY_DECIMAL_PLACES);
   }
}

///
/// <summary>
/// Formats the latest PM1.0, PM2.5, PM10, CO2, temperature, humidity, VOC, NOx,
/// airQuality score/tier, and SGP41 conditioning state for the "GetReadings" command
/// registered via sketch.onReadings().
/// </summary>
/// <returns>Formatted readings text.</returns>
///
static std::string getReadings()
{
   auto formatVal = [](float val, int decimals, const char* unit) -> std::string
   {
      if (isnan(val))
      {
         return "NAN";
      }
      std::ostringstream out;
      out.precision(decimals);
      out << std::fixed << val;
      if (unit != nullptr && unit[0] != '\0')
      {
         out << " " << unit;
      }
      return out.str();
   };

   int8_t worstTier = getWorstTier();
   float worstScore = getWorstAirQualityScore();
   std::string airQualityText = (worstTier >= 0 && !isnan(worstScore))
      ? formatVal(worstScore, AIR_QUALITY_DECIMAL_PLACES, "") + " (" + AirQualityTiers::TIER_NAMES[worstTier] + ")"
      : "NAN";

   return
      "pm1=" + formatVal(pm1, 1, "ug/m3") + "\n" +
      "pm25=" + formatVal(pm25, 1, "ug/m3") + "\n" +
      "pm10=" + formatVal(pm10, 1, "ug/m3") + "\n" +
      "co2=" + formatVal(co2, 0, "ppm") + "\n" +
      "temperature=" + formatVal(tempF, 1, "F") + "\n" +
      "humidity=" + formatVal(humidity, 1, "%") + "\n" +
      "voc=" + formatVal(vocIndex, 0, "") + "\n" +
      "nox=" + formatVal(noxIndex, 1, "") + "\n" +
      "airQuality=" + airQualityText + "\n" +
      "sgpConditioned=" + (sgpConditioned ? "true" : "false");
}

///
/// <summary>
/// Applies the 3D-printer-room tuning parameters to the host VOC algorithm and restores a
/// previously saved clean-air baseline from Preferences if one exists.
/// </summary>
/// <returns>True if a saved baseline was found and restored.</returns>
///
bool initVocAlgorithm()
{
   vocAlgorithm.set_tuning_parameters(
      VOC_INDEX_OFFSET,
      VOC_LEARNING_TIME_OFFSET_H,
      VOC_LEARNING_TIME_GAIN_H,
      VOC_GATING_MAX_DURATION_M,
      VOC_STD_INITIAL,
      VOC_GAIN_FACTOR);

   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
   float state0 = sketch.arduino.preferences.getFloat(VOC_STATE0_PREF_KEY, NAN);
   float state1 = sketch.arduino.preferences.getFloat(VOC_STATE1_PREF_KEY, NAN);
   sketch.arduino.preferences.end();

   if (!isnan(state0) && !isnan(state1) && state0 > 0.0f && state1 > 0.0f)
   {
      vocAlgorithm.set_states(state0, state1);
      return true;
   }

   return false;
}

///
/// <summary>
/// Saves the current SGP41 VOC algorithm state to Preferences as the clean-air baseline.
/// </summary>
/// <returns>True if the state is valid and was saved to Preferences.</returns>
///
bool saveSgp41VocBaseline()
{
   float state0 = 0.0f;
   float state1 = 0.0f;
   vocAlgorithm.get_states(state0, state1);
   if (state0 <= 0.0f || state1 <= 0.0f)
   {
      return false;
   }

   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   size_t written0 = sketch.arduino.preferences.putFloat(VOC_STATE0_PREF_KEY, state0);
   size_t written1 = sketch.arduino.preferences.putFloat(VOC_STATE1_PREF_KEY, state1);
   sketch.arduino.preferences.end();

   return written0 > 0 && written1 > 0;
}

///
/// <summary>
/// Executes the clean-air calibration cycle: recalibrates SCD41 CO2 to targetPpm, clears
/// any saved SGP41 VOC baseline, resets VOC learning, and starts the automatic
/// baseline-save timer.
/// </summary>
/// <param name="targetPpm">Reference CO2 in ppm (400-2000).</param>
///
static void executeCalibration(int targetPpm)
{
   std::ostringstream result;

   scd41.stopPeriodicMeasurement();
   uint16_t frcCorrection = 0;
   int16_t err = scd41.performForcedRecalibration(static_cast<uint16_t>(targetPpm), frcCorrection);
   scd41.startPeriodicMeasurement();

   if (err != 0 || frcCorrection == 0xFFFF)
   {
      result << "SCD41 CO2 recalibration failed. ";
   }
   else
   {
      int32_t correctionPpm = static_cast<int32_t>(frcCorrection) - 0x8000;
      result << "Calibrated CO2 to " << targetPpm << " ppm (correction=" << correctionPpm << " ppm). ";
   }

   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.remove(VOC_STATE0_PREF_KEY);
   sketch.arduino.preferences.remove(VOC_STATE1_PREF_KEY);
   sketch.arduino.preferences.end();

   initVocAlgorithm();
   vocIndex = NAN;
   vocCalibrating = true;
   vocCalibrationTimer.reset();

   result << "Started " << static_cast<int>(VOC_CALIBRATION_M)
          << "-minute SGP41 VOC clean-air calibration; baseline will be saved automatically when complete.";
   DeviceServerClient::respond(result.str().c_str());
}

static int pendingCalibrationPpm = 400;

///
/// <summary>
/// Handles the y/n confirmation answer for the interactive shell "Calibrate" prompt.
/// </summary>
/// <param name="line">User response entered in the shell.</param>
///
static void onCalibrateConfirm(const char* line)
{
   String input = String(line);
   input.trim();

   if (input.equalsIgnoreCase("y") || input.equalsIgnoreCase("yes"))
   {
      executeCalibration(pendingCalibrationPpm);
   }
   else
   {
      DeviceServerClient::respond("Calibration canceled.");
   }
}

///
/// <summary>
/// Commands registered with DeviceServerClient, listed by "Help" after the built-ins.
/// </summary>
static constexpr DeviceServerClient::CommandInfo AIR_MONITOR_COMMANDS[] = {
   {
      "Calibrate",
      "Calibrates CO2 to the reference ppm and runs a 3-hour clean-air VOC baseline calibration",
      { "cal", "fc" },
      {
         { "ppm", "Reference CO2 in ppm (400-2000)", false, "400" },
      },
      true,
   },
};

///
/// <summary>
/// Handles custom commands registered via sketch.setCommandHandler().
/// </summary>
/// <param name="command">Full command text received from the DeviceServer.</param>
static void onAirMonitorCommand(const char* command)
{
   std::istringstream stream(command);
   std::string verb;
   stream >> verb;

   if (strcasecmp(verb.c_str(), "Calibrate") == 0)
   {
      int targetPpm = 400;
      if (stream >> targetPpm)
      {
         if (targetPpm < 400 || targetPpm > 2000)
         {
            DeviceServerClient::respond("Error: target ppm must be between 400 and 2000");
            return;
         }
      }

      if (DeviceServerClient::isShellCommand())
      {
         pendingCalibrationPpm = targetPpm;
         DeviceServerClient::shell().println("Clean-air calibration will:");
         DeviceServerClient::shell().print("  1. Recalibrate the SCD41 CO2 sensor to ");
         DeviceServerClient::shell().print(targetPpm);
         DeviceServerClient::shell().println(" ppm.");
         DeviceServerClient::shell().println("  2. Clear the saved VOC baseline and restart SGP41 VOC learning.");
         DeviceServerClient::shell().print("  3. Automatically save the new clean-air VOC baseline after ");
         DeviceServerClient::shell().print(VOC_CALIBRATION_M);
         DeviceServerClient::shell().println(" minutes (3 hours).");
         DeviceServerClient::shell().println("Ensure the room is well ventilated and the 3D printer remains off for the next 3 hours.");
         DeviceServerClient::shell().print("Continue? (y/N): ");
         DeviceServerClient::shell().flushOutput();
         DeviceServerClient::promptShellLine(onCalibrateConfirm);
         return;
      }

      executeCalibration(targetPpm);
   }
   else
   {
      DeviceServerClient::respond((std::string("Unhandled command: ") + command).c_str());
   }
}

void setup()
{
   initVocAlgorithm();

   sketch.addSensor("PMS5003", []() { return pms.begin(&Serial1, PMS_RX_PIN, PMS_TX_PIN); });
   sketch.addSensor("SCD41", []()
   {
      scd41.begin(Wire, SCD41_I2C_ADDR_62);
      scd41.wakeUp();
      scd41.stopPeriodicMeasurement();
      scd41.reinit();
      return scd41.startPeriodicMeasurement() == 0;
   });
   sketch.addSensor("SGP41", []()
   {
      sgp41.begin(Wire);
      uint16_t serial[3];
      return sgp41.getSerialNumber(serial) == 0;
   });
   sketch.setValueSource([]() { return pm25; });
   sketch.onSiteResolved(onSiteResolved);
   sketch.onReadings(getReadings, "Reports the latest PM1.0, PM2.5, PM10, CO2, temperature, humidity, VOC, NOx, and SGP41 conditioning state");
   sketch.setCommandHandler(onAirMonitorCommand, AIR_MONITOR_COMMANDS, std::size(AIR_MONITOR_COMMANDS));

   sketch.begin();

   InfluxPoint* point = sketch.addPoint({ { "item", "PMS5003" } });
   pm1Field = point->addTimeAverageField(INFLUX_INTERVAL_S, "pm1", INFLUX_DECIMAL_PLACES);
   pm1QualityField = point->addTimeAverageField(INFLUX_INTERVAL_S, "pm1Quality", AIR_QUALITY_DECIMAL_PLACES);
   pm25Field = point->addTimeAverageField(INFLUX_INTERVAL_S, "pm25", INFLUX_DECIMAL_PLACES);
   pm25QualityField = point->addTimeAverageField(INFLUX_INTERVAL_S, "pm25Quality", AIR_QUALITY_DECIMAL_PLACES);
   pm10Field = point->addTimeAverageField(INFLUX_INTERVAL_S, "pm10", INFLUX_DECIMAL_PLACES);
   pm10QualityField = point->addTimeAverageField(INFLUX_INTERVAL_S, "pm10Quality", AIR_QUALITY_DECIMAL_PLACES);

   table.addRow("PM1.0", &pm1Value);
   table.addRow("PM2.5", &pm25Value);
   table.addRow("PM10", &pm10Value);

   InfluxPoint* scdPoint = sketch.addPoint({ { "item", "SCD41" } });
   co2Field = scdPoint->addTimeAverageField(INFLUX_INTERVAL_S, "co2", INFLUX_DECIMAL_PLACES);
   co2QualityField = scdPoint->addTimeAverageField(INFLUX_INTERVAL_S, "co2Quality", AIR_QUALITY_DECIMAL_PLACES);
   scdTempField = scdPoint->addTimeAverageField(INFLUX_INTERVAL_S, "temperature", INFLUX_DECIMAL_PLACES);
   humidityField = scdPoint->addTimeAverageField(INFLUX_INTERVAL_S, "humidity", INFLUX_DECIMAL_PLACES);

   InfluxPoint* sgpPoint = sketch.addPoint({ { "item", "SGP41" } });
   vocField = sgpPoint->addTimeAverageField(INFLUX_INTERVAL_S, "voc", INFLUX_DECIMAL_PLACES);
   vocQualityField = sgpPoint->addTimeAverageField(INFLUX_INTERVAL_S, "vocQuality", AIR_QUALITY_DECIMAL_PLACES);
   noxField = sgpPoint->addTimeAverageField(INFLUX_INTERVAL_S, "nox", INFLUX_DECIMAL_PLACES);
   noxQualityField = sgpPoint->addTimeAverageField(INFLUX_INTERVAL_S, "noxQuality", AIR_QUALITY_DECIMAL_PLACES);

   InfluxPoint* airQualityPoint = sketch.addPoint();
   airQualityField = airQualityPoint->addTimeAverageField(INFLUX_INTERVAL_S, "airQuality", AIR_QUALITY_DECIMAL_PLACES);

   table.addRow("CO2", &co2Value);
   table.addRow("VOC", &vocValue);
   table.addRow("NOx", &noxValue);
   table.setPosition(sketch.arduino.width() / 2, sketch.arduino.height() / 2, Anchor::CENTER);

   // Pause so the initialization info can be read before it's replaced with the table.
   delay(STARTUP_DELAY_S * 1000UL);

   sketch.arduino.clearDisplay();

   Logger.logInitializationComplete();
}

///
/// <summary>
/// Draws the header line at the top of the display.
/// </summary>
///
void drawHeader()
{
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.setTextSize(TEXT_SIZE_SMALL);
   sketch.arduino.print("Air", Color::HEADING);
   sketch.arduino.println();
}

///
/// <summary>
/// Draws the footer line (site/location + version) at the bottom of the display.
/// </summary>
///
void drawFooter()
{
   sketch.arduino.setTextSize(TEXT_SIZE_SMALL);
   sketch.arduino.setCursor(0, -sketch.arduino.charH());
   sketch.arduino.print(std::string(sketch.context().location ? sketch.context().location : ""), Color::CYAN);
   sketch.arduino.printR(VERSION, Color::SUB_LABEL);
}

void loop()
{
   sketch.loop();

   if (pms.read())
   {
      pm1Field->set(pms.pm1());
      pm1QualityField->set(AirQualityTiers::scoreFor(pms.pm1(), AirQualityTiers::PM25_LIMITS));
      pm25Field->set(pms.pm25());
      pm25QualityField->set(AirQualityTiers::scoreFor(pms.pm25(), AirQualityTiers::PM25_LIMITS));
      pm10Field->set(pms.pm10());
      pm10QualityField->set(AirQualityTiers::scoreFor(pms.pm10(), AirQualityTiers::PM10_LIMITS));

      pm1 = pm1Field->get();
      pm25 = pm25Field->get();
      pm10 = pm10Field->get();

      sketch.publish(pm1Topic, pm1, TELEMETRY_DECIMAL_PLACES);
      sketch.publish(pm10Topic, pm10, TELEMETRY_DECIMAL_PLACES);
      publishAirQuality();
   }

   bool dataReady = false;
   if (scd41.getDataReadyStatus(dataReady) == 0 && dataReady)
   {
      uint16_t co2Ppm = 0;
      float tempC = 0;
      float rh = 0;
      if (scd41.readMeasurement(co2Ppm, tempC, rh) == 0 && co2Ppm != 0)
      {
         lastTempC = tempC;
         lastHumidity = rh;
         co2 = co2Ppm;
         tempF = tempC * 9.0f / 5.0f + 32.0f;
         humidity = rh;

         co2Field->set(co2);
         co2QualityField->set(AirQualityTiers::scoreFor(co2, AirQualityTiers::CO2_LIMITS));
         scdTempField->set(tempF);
         humidityField->set(humidity);

         sketch.publish(co2Topic, co2, 0);
         publishAirQuality();
      }
   }

   if (sgpTimer.ready())
   {
      uint16_t rhTicks = static_cast<uint16_t>(lastHumidity * 65535.0f / 100.0f);
      uint16_t tempTicks = static_cast<uint16_t>((lastTempC + 45.0f) * 65535.0f / 175.0f);
      uint16_t rawVoc = 0;
      uint16_t rawNox = 0;

      if (!sgpConditioned)
      {
         sgp41.executeConditioning(rhTicks, tempTicks, rawVoc);
         sgpConditioningCount++;
         if (sgpConditioningCount >= SGP41_CONDITIONING_S)
         {
            sgpConditioned = true;
         }
      }
      else if (sgp41.measureRawSignals(rhTicks, tempTicks, rawVoc, rawNox) == 0)
      {
         vocIndex = vocAlgorithm.process(rawVoc);
         int32_t rawNoxIndex = noxAlgorithm.process(rawNox);

         // The gas index algorithms report 0 during their initial blackout period (see
         // GasIndexAlgorithm_INITIAL_BLACKOUT), before they've established a baseline.
         // That's not a real reading, so don't upload or publish it.
         if (vocIndex != 0)
         {
            vocField->set(vocIndex);
            vocQualityField->set(AirQualityTiers::scoreFor(vocIndex, AirQualityTiers::VOC_LIMITS));
            sketch.publish(vocTopic, vocIndex, 0);
         }
         if (rawNoxIndex != 0)
         {
            noxField->set(rawNoxIndex);
            noxQualityField->set(AirQualityTiers::scoreFor(rawNoxIndex, AirQualityTiers::NOX_LIMITS));
            noxIndex = noxField->get();
            sketch.publish(noxTopic, noxIndex, TELEMETRY_DECIMAL_PLACES);
         }
         else
         {
            noxIndex = 0;
         }
         if (vocIndex != 0 || rawNoxIndex != 0)
         {
            publishAirQuality();
         }
      }
   }

   if (vocCalibrating && vocCalibrationTimer.ready())
   {
      vocCalibrating = false;
      if (saveSgp41VocBaseline())
      {
         Logger.log("Saved clean-air SGP41 VOC baseline to Preferences");
      }
      else
      {
         Logger.log("Failed to save SGP41 VOC baseline to Preferences", LogSeverity::ERROR);
      }
   }

   if (influxTimer.ready())
   {
      sketch.postPoints();
   }

   drawHeader();
   table.draw();
   drawFooter();
}
