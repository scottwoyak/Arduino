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
// - Telemetry: topics are item/site/location, published as PM1.0, PM2.5, PM10, CO2, VOC
//   and NOx (e.g. "PM2.5/site/location").
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

// Declares which VLW font sizes this sketch actually uses, so only the needed font data
// is compiled in.
#define TEXT_SIZES_CUSTOM
#define TEXT_SIZE_2

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#include "WrongBoard.h"
#endif

#include <NOxGasIndexAlgorithm.h>
#include <SensirionI2cScd4x.h>
#include <SensirionI2CSgp41.h>
#include <VOCGasIndexAlgorithm.h>
#include <Wire.h>

#include "FieldTable.h"
#include "LibraryVersion.h"
#include "PMS5003.h"
#include "Timer.h"

#include "WiFiSettings.h"

#include "PublisherSketch.h"

// This sketch's own version (e.g. "1.0"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Air_Monitor_Display";
constexpr auto PM1_TOPIC_PREFIX = "PM1.0";
constexpr auto PM25_TOPIC_PREFIX = "PM2.5";
constexpr auto PM10_TOPIC_PREFIX = "PM10";
constexpr auto CO2_TOPIC_PREFIX = "CO2";
constexpr auto VOC_TOPIC_PREFIX = "VOC";
constexpr auto NOX_TOPIC_PREFIX = "NOx";
constexpr uint16_t SGP41_CONDITIONING_S = 10;
constexpr size_t MAX_TOPIC_LENGTH = 64;
constexpr uint8_t TELEMETRY_DECIMAL_PLACES = 0;
constexpr uint8_t TEXT_SIZE_SMALL = 2;
constexpr uint8_t STARTUP_DELAY_S = 5;
constexpr uint8_t INFLUX_INTERVAL_S = 60;
constexpr uint8_t INFLUX_DECIMAL_PLACES = 1;
constexpr uint8_t PMS_RX_PIN = RX;
constexpr uint8_t PMS_TX_PIN = TX;

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
}

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = pm25Topic,
   .decimals = TELEMETRY_DECIMAL_PLACES,
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = "AirMonitor",
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
InfluxField* scdTempField = nullptr;
InfluxField* humidityField = nullptr;
InfluxField* vocField = nullptr;
InfluxField* noxField = nullptr;

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
InfluxField* pm25Field = nullptr;
InfluxField* pm10Field = nullptr;

Timer influxTimer(INFLUX_INTERVAL_S * 1000UL);

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

///
/// <summary>
/// Picks a color from ascending thresholds: green up to the first limit, then yellow,
/// orange, and red beyond the last limit.
/// </summary>
/// <param name="value">The value to rate.</param>
/// <param name="greenMax">Highest value still rated green.</param>
/// <param name="yellowMax">Highest value still rated yellow.</param>
/// <param name="orangeMax">Highest value still rated orange.</param>
/// <returns>The rating color.</returns>
///
Color rateHighIsBad(float value, float greenMax, float yellowMax, float orangeMax)
{
   if (isnan(value))
   {
      return Color::VALUE;
   }
   if (value <= greenMax)
   {
      return Color::LIME;
   }
   if (value <= yellowMax)
   {
      return Color::YELLOW;
   }
   if (value <= orangeMax)
   {
      return Color::ORANGE;
   }
   return Color::RED;
}

Color co2Color(float value)
{
   return rateHighIsBad(value, 800, 1000, 1500);
}

Color vocColor(float value)
{
   return rateHighIsBad(value, 150, 250, 400);
}

Color noxColor(float value)
{
   return rateHighIsBad(value, 20, 100, 300);
}

// PM ratings use EPA AQI breakpoints (ug/m3).
Color pm1Color(float value)
{
   return rateHighIsBad(value, 9, 35.4f, 55.4f);
}

Color pm25Color(float value)
{
   return rateHighIsBad(value, 9, 35.4f, 55.4f);
}

Color pm10Color(float value)
{
   return rateHighIsBad(value, 54, 154, 254);
}

ColoredValue pm1Value("### ug/m3", &pm1, pm1Color);
ColoredValue pm25Value("### ug/m3", &pm25, pm25Color);
ColoredValue pm10Value("### ug/m3", &pm10, pm10Color);
ColoredValue co2Value("#### ppm", &co2, co2Color);
ColoredValue vocValue("###", &vocIndex, vocColor);
ColoredValue noxValue("###", &noxIndex, noxColor);
FieldTable table(&sketch.arduino, 0, 0, TEXT_SIZE_SMALL);
void setup()
{
   Wire.begin();

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
   sketch.setValueSource([]() { return pms.pm25(); });
   sketch.onSiteResolved(onSiteResolved);

   sketch.setOnWiFiLostCallback([]()
   {
      sketch.arduino.clearDisplay();
      sketch.arduino.println("WiFi connection lost", Color::RED);
      return false;
   });

   sketch.begin();

   InfluxPoint* point = sketch.addPoint({ { "item", "PMS5003" } });
   pm1Field = point->addTimeAverageField(INFLUX_INTERVAL_S, "pm1", INFLUX_DECIMAL_PLACES);
   pm25Field = point->addTimeAverageField(INFLUX_INTERVAL_S, "pm25", INFLUX_DECIMAL_PLACES);
   pm10Field = point->addTimeAverageField(INFLUX_INTERVAL_S, "pm10", INFLUX_DECIMAL_PLACES);

   table.addRow("PM1.0", &pm1Value);
   table.addRow("PM2.5", &pm25Value);
   table.addRow("PM10", &pm10Value);

   InfluxPoint* scdPoint = sketch.addPoint({ { "item", "SCD41" } });
   co2Field = scdPoint->addTimeAverageField(INFLUX_INTERVAL_S, "co2", 0);
   scdTempField = scdPoint->addTimeAverageField(INFLUX_INTERVAL_S, "temperature", INFLUX_DECIMAL_PLACES);
   humidityField = scdPoint->addTimeAverageField(INFLUX_INTERVAL_S, "humidity", INFLUX_DECIMAL_PLACES);

   InfluxPoint* sgpPoint = sketch.addPoint({ { "item", "SGP41" } });
   vocField = sgpPoint->addTimeAverageField(INFLUX_INTERVAL_S, "voc", 0);
   noxField = sgpPoint->addTimeAverageField(INFLUX_INTERVAL_S, "nox", 0);

   table.addRow("CO2", &co2Value);
   table.addRow("VOC", &vocValue);
   table.addRow("NOx", &noxValue);
   table.setPosition(sketch.arduino.width() / 2, sketch.arduino.height() / 2, Anchor::CENTER);

   sketch.setOnLocateEndCallback([]() { table.invalidate(); });

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

   if (sketch.isLocating())
   {
      return;
   }

   if (pms.read())
   {
      pm1 = pms.pm1();
      pm25 = pms.pm25();
      pm10 = pms.pm10();

      pm1Field->set(pm1);
      pm25Field->set(pm25);
      pm10Field->set(pm10);

      sketch.publish(pm1Topic, pm1, TELEMETRY_DECIMAL_PLACES);
      sketch.publish(pm10Topic, pm10, TELEMETRY_DECIMAL_PLACES);
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
         scdTempField->set(tempF);
         humidityField->set(humidity);

         sketch.publish(co2Topic, co2, 0);
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
         noxIndex = noxAlgorithm.process(rawNox);

         vocField->set(vocIndex);
         noxField->set(noxIndex);

         sketch.publish(vocTopic, vocIndex, 0);
         sketch.publish(noxTopic, noxIndex, 0);
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
