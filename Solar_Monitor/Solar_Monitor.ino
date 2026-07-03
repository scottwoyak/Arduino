/// <summary>
/// Solar power system monitoring station with real-time display and InfluxDB logging.
/// </summary>
/// <remarks>
/// Monitors battery, solar input, and load power using three INA219 current sensors.
/// Displays metrics on a TFT screen in real-time and logs to InfluxDB at configurable intervals.
/// Tracks cumulative charge/discharge in mAh. Resets accumulators on button press.
/// 
/// Hardware: ESP32-S3 Feather with ST7796S TFT display, 3x INA219 power monitors.
/// </remarks>

#include <Arduino.h>
#include <Adafruit_INA219.h>
#include <cmath>

#include "Feather_ESP32_S3_ST7796S.h"
#include "Influx.h"
#include "SerialX.h"
#include "TimedAverage.h"
#include "Util.h"

#include "WiFiSettings.h"

// Display pins
constexpr auto TFT_BACKLIGHT_PIN = 5;
constexpr auto TFT_DC_PIN = 6;
constexpr auto TFT_RST_PIN = -1;
constexpr auto TFT_CS_PIN = 9;

// INA219 sensor I2C addresses
constexpr auto BATTERY_SENSOR_ADDR = 0x40;
constexpr auto SOLAR_SENSOR_ADDR = 0x41;   // A0 shorted to ground
constexpr auto LOAD_SENSOR_ADDR = 0x44;    // A1 shorted to ground

// InfluxDB settings
constexpr auto INFLUX_MEASUREMENT = "Power";
constexpr auto INFLUX_INTERVAL_S = 10;
constexpr auto WIFI_RESET_DELAY_S = 10;

// Format strings for display
Format voltsFormat("#.## v");
Format currentFormat("####.# mA");
Format mAhFormat("+####.## mAh");
Format mAhFormat2("####.## mAh");

Feather_ESP32_S3_ST7796S feather(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN, TFT_BACKLIGHT_PIN);

Adafruit_INA219 batterySensor(BATTERY_SENSOR_ADDR);
Adafruit_INA219 solarSensor(SOLAR_SENSOR_ADDR);
Adafruit_INA219 loadSensor(LOAD_SENSOR_ADDR);

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Influx influx(WIFI_SSID, WIFI_PASSWORD, &client);

// Data point definitions for InfluxDB
InfluxPoint batteryPoint(INFLUX_MEASUREMENT, {{"item", "Battery"}});
InfluxPoint solarPoint(INFLUX_MEASUREMENT, {{"item", "Solar"}});
InfluxPoint loadPoint(INFLUX_MEASUREMENT, {{"item", "Load"}});
Timer influxTimer(INFLUX_INTERVAL_S * 1000);

// Field references for data points
InfluxField* batteryVoltsField = batteryPoint.addTimeAveragedField("volts", 3);
InfluxField* batterymAField = batteryPoint.addTimeAveragedField("mA", 1);
InfluxField* batterymAhField = batteryPoint.addValueField("mAh", 1);
InfluxField* solarVoltsField = solarPoint.addTimeAveragedField("volts", 3);
InfluxField* solarmAField = solarPoint.addTimeAveragedField("mA", 1);
InfluxField* loadVoltsField = loadPoint.addTimeAveragedField("volts", 3);
InfluxField* loadmAField = loadPoint.addTimeAveragedField("mA", 1);
InfluxField* loadmAhField = loadPoint.addValueField("mAh", 1);

// Display smoothing/averaging
TimedAverage displayBatteryVolts(1000);
TimedAverage displayBatterymA(1000);
TimedAverage displaySolarVolts(1000);
TimedAverage displaySolarmA(1000);
TimedAverage displayLoadVolts(1000);
TimedAverage displayLoadmA(1000);

// Charge/discharge accumulators
unsigned long lastMicros = 0;
double batterymAh = 0;
double loadmAh = 0;

/// <summary>
/// Initializes and verifies an INA219 sensor is present.
/// Halts execution if sensor not found.
/// </summary>
/// <param name="sensor">Reference to INA219 sensor</param>
/// <param name="name">Human-readable sensor name for display</param>
void initializeSensor(Adafruit_INA219& sensor, const char* name)
{
   feather.print(name, Color::LABEL);
   feather.print("... ");

   if (!sensor.begin())
   {
      Serial.print(name);
      Serial.println(" Not Found");

      feather.setTextSize(3);
      feather.display.fillScreen((uint16_t)Color::RED);
      feather.setCursor(0, feather.display.height() / 2 - feather.charH());
      feather.printlnC(name, Color::WHITE, Color::RED);
      feather.printlnC("Not Found", Color::WHITE, Color::RED);

      Util::reset(WIFI_RESET_DELAY_S);
   }

   feather.printlnR("OK", Color::VALUE);
}

/// <summary>
/// Updates charge/discharge accumulators based on current measurements.
/// </summary>
void updateChargeAccumulators(float batterymA, float loadmA)
{
   unsigned long now = micros();

   if (lastMicros != 0)
   {
      double elapsedHours = (now - lastMicros) / (60.0 * 60.0 * 1000.0 * 1000.0);
      batterymAh += (batterymA * elapsedHours);
      loadmAh += (loadmA * elapsedHours);
   }

   lastMicros = now;
}

/// <summary>
/// Posts a data point to InfluxDB with WiFi retry.
/// </summary>
/// <param name="point">InfluxPoint to post</param>
/// <param name="pointName">Name of point for logging</param>
void postDataPoint(InfluxPoint& point, const char* pointName)
{
   if (!influx.ensureWiFiConnected())
   {
      Serial.print(pointName);
      Serial.println(" - WiFi reconnection failed");
      return;
   }

   digitalWrite(BUILTIN_LED, HIGH);

   if (!point.post(&client, true))
   {
      Serial.print(pointName);
      Serial.print(" - InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
   }

   digitalWrite(BUILTIN_LED, LOW);
}

/// <summary>
/// Returns color based on charging direction (green=charging, red=draining).
/// </summary>
uint16_t getChargeDirectionColor(float currentmA)
{
   return currentmA >= 0 ? Color565::fromRGB(100, 255, 100) : Color565::fromRGB(255, 100, 100);
}

void setup()
{
   SerialX::begin();

   pinMode(BUILTIN_LED, OUTPUT);
   feather.begin();
   feather.display.setRotation(1);

   Influx::startInit(&feather);

   // Initialize all three power sensors
   initializeSensor(batterySensor, "INA219 (Battery)");
   initializeSensor(solarSensor, "INA219 (Solar)");
   initializeSensor(loadSensor, "INA219 (Load)");

   if (!influx.begin(&feather))
   {
      Util::reset(WIFI_RESET_DELAY_S);
   }
   Influx::endInit(&feather);
}

void loop()
{
   // Reset accumulators on button press
   if (feather.buttonA.wasPressed())
   {
      batterymAh = 0;
      loadmAh = 0;
   }

   // Read voltage and current from all sensors
   float nowBatteryVolts = batterySensor.getBusVoltage_V() + batterySensor.getShuntVoltage_mV() / 1000.0f;
   float nowBatterymA = batterySensor.getCurrent_mA();

   float nowSolarVolts = solarSensor.getBusVoltage_V() + solarSensor.getShuntVoltage_mV() / 1000.0f;
   float nowSolarmA = solarSensor.getCurrent_mA();

   float nowLoadVolts = loadSensor.getBusVoltage_V() + loadSensor.getShuntVoltage_mV() / 1000.0f;
   float nowLoadmA = loadSensor.getCurrent_mA();

   // Update charge accumulators
   updateChargeAccumulators(nowBatterymA, nowLoadmA);

   // Update display averages
   displayBatteryVolts.set(nowBatteryVolts);
   displayBatterymA.set(nowBatterymA);
   displayLoadVolts.set(nowLoadVolts);
   displayLoadmA.set(nowLoadmA);
   displaySolarVolts.set(nowSolarVolts);
   displaySolarmA.set(nowSolarmA);

   // Update InfluxDB field values
   batteryVoltsField->set(nowBatteryVolts);
   batterymAField->set(nowBatterymA);
   batterymAhField->set(batterymAh);
   solarVoltsField->set(nowSolarVolts);
   solarmAField->set(nowSolarmA);
   loadVoltsField->set(nowLoadVolts);
   loadmAField->set(nowLoadmA);
   loadmAhField->set(loadmAh);

   // Render display
   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Solar Monitor", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.setTextSize(2);

   // Battery section
   uint16_t batteryChargeColor = getChargeDirectionColor(displayBatterymA.get());
   feather.print("Battery: ", Color::HEADING2);
   feather.println(displayBatterymA.get() >= 0 ? "Charging" : "Draining", batteryChargeColor);
   feather.moveCursorY(2);
   feather.println("   Volts: ", displayBatteryVolts.get(), voltsFormat);
   feather.moveCursorY(1);
   feather.println(" Current: ", std::abs(displayBatterymA.get()), currentFormat, batteryChargeColor);
   feather.moveCursorY(1);
   feather.println("     mAh: ", batterymAh, mAhFormat, batteryChargeColor);

   // Load section
   feather.moveCursorY(feather.charH());
   feather.println("Load", Color::HEADING2);
   feather.moveCursorY(2);
   feather.println("   Volts: ", displayLoadVolts.get(), voltsFormat);
   feather.moveCursorY(1);
   feather.println(" Current: ", std::abs(displayLoadmA.get()), currentFormat);
   feather.moveCursorY(1);
   feather.println("     mAh: ", loadmAh, mAhFormat2);

   // Solar section
   feather.moveCursorY(feather.charH());
   feather.println("Solar", Color::HEADING2);
   feather.moveCursorY(2);
   feather.println("   Volts: ", displaySolarVolts.get(), voltsFormat);
   feather.moveCursorY(1);
   feather.println(" Current: ", displaySolarmA.get(), currentFormat);

   // Upload data points to InfluxDB
   if (influxTimer.ready())
   {
      postDataPoint(batteryPoint, "Battery");
      postDataPoint(solarPoint, "Solar");
      postDataPoint(loadPoint, "Load");
   }
}
