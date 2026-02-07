
#include "Feather_ESP32_S3_ST7796S.h"
#include "Adafruit_INA219.h"
#include "SerialX.h"
#include "TimedAverager.h"
#include "Influx.h"
#include "WiFiSettings.h"
#include "Util.h"

constexpr auto PIN_BACKLITE = 5;
constexpr auto PIN_DC = 6;
constexpr auto PIN_RST = -1;
constexpr auto PIN_CS = 9;
Feather_ESP32_S3_ST7796S feather(PIN_CS, PIN_DC, PIN_RST, PIN_BACKLITE);

Adafruit_INA219 sensor1(0x40);
Adafruit_INA219 sensor2(0x41); // A0 shorted
Adafruit_INA219 sensor3(0x44); // A1 shorted

Format voltsFormat("#.## v");
Format currentFormat("####.# mA");
Format percentFormat("###.#%");
Format mAhFormat("+####.## mAh");
Format mAhFormat2("####.## mAh");

TimedAverager displayBatteryVolts(1000);
TimedAverager displayBatterymA(1000);
TimedAverager displaySolarVolts(1000);
TimedAverager displaySolarmA(1000);
TimedAverager displayLoadVolts(1000);
TimedAverager displayLoadmA(1000);

constexpr auto INFLUX_MEASUREMENT = "Power";
constexpr auto INFLUX_INTERVAL_S = 10;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

TimedPoint batteryPoint(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT, { {"item", "Battery"} });
TimedPoint solarPoint(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT, { { "item", "Solar" } });
TimedPoint loadPoint(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT, { { "item", "Load" } });

Field* batteryVoltsField = batteryPoint.addTimeAveragedField("volts", 3);
Field* batterymAField = batteryPoint.addTimeAveragedField("mA", 1);
Field* batterymAhField = batteryPoint.addValueField("mAh", 1);
Field* solarVoltsField = solarPoint.addTimeAveragedField("volts", 3);
Field* solarmAField = solarPoint.addTimeAveragedField("mA", 1);
Field* loadVoltsField = loadPoint.addTimeAveragedField("volts", 3);
Field* loadmAField = loadPoint.addTimeAveragedField("mA", 1);
Field* loadmAhField = loadPoint.addValueField("mAh", 1);

void setup()
{
   pinMode(BUILTIN_LED, OUTPUT);
   feather.begin();
   feather.display.setRotation(1);
   SerialX::begin();

   Influx::startInit(&feather);

   feather.print("INA219 (Battery)... ", Color::LABEL);
   if (!sensor1.begin())
   {
      Serial.println("INA219 (Battery) Not Found");
      feather.setTextSize(3);
      feather.display.fillScreen((uint16_t)Color::RED);
      feather.setCursor(0, feather.display.height() / 2 - feather.charH());
      feather.printlnC("Battery INA219", Color::WHITE, Color::RED);
      feather.printlnC("Not Found", Color::WHITE, Color::RED);
      while (1);
   }
   feather.printR("ok", Color::VALUE);
   feather.println();

   feather.print("INA219 (Solar)... ", Color::LABEL);
   if (!sensor2.begin())
   {
      Serial.println("INA219 (Solar) Not Found");
      feather.setTextSize(3);
      feather.display.fillScreen((uint16_t)Color::RED);
      feather.setCursor(0, feather.display.height() / 2 - feather.charH());
      feather.printlnC("Solar INA219", Color::WHITE, Color::RED);
      feather.printlnC("Not Found", Color::WHITE, Color::RED);
      while (1);
   }
   feather.printR("ok", Color::VALUE);
   feather.println();

   feather.print("INA219 (Load)... ", Color::LABEL);
   if (!sensor3.begin())
   {
      Serial.println("INA219 (Load) Not Found");
      feather.setTextSize(3);
      feather.display.fillScreen((uint16_t)Color::RED);
      feather.setCursor(0, feather.display.height() / 2 - feather.charH());
      feather.printlnC("Load INA219", Color::WHITE, Color::RED);
      feather.printlnC("Not Found", Color::WHITE, Color::RED);
      while (1);
   }
   feather.printR("ok", Color::VALUE);
   feather.println();

   Influx::begin(&feather, WIFI_SSID, WIFI_PASSWORD, &client);

   Influx::endInit(&feather);
}

unsigned long lastMicros = 0;
double batterymAh = 0;
double loadmAh = 0;

void loop()
{
   if (feather.buttonA.wasPressed())
   {
      batterymAh = 0;
      loadmAh = 0;
   }

   float nowBatteryVolts = sensor1.getBusVoltage_V() + sensor1.getShuntVoltage_mV() / 1000;
   float nowBatterymA = sensor1.getCurrent_mA();

   float nowSolarVolts = sensor2.getBusVoltage_V() + sensor2.getShuntVoltage_mV() / 1000;
   float nowSolarmA = sensor2.getCurrent_mA();

   float nowLoadVolts = sensor3.getBusVoltage_V() + sensor3.getShuntVoltage_mV() / 1000;
   float nowLoadmA = sensor3.getCurrent_mA();

   unsigned long now = micros();
   if (lastMicros != 0)
   {
      double hours = (now - lastMicros) / (60.0 * 60 * 1000.0 * 1000);
      batterymAh += (nowBatterymA * hours);
      loadmAh += (nowLoadmA * hours);
   }
   lastMicros = now;


   displayBatteryVolts.set(nowBatteryVolts);
   displayBatterymA.set(nowBatterymA);
   displayLoadVolts.set(nowLoadVolts);
   displayLoadmA.set(nowLoadmA);
   displaySolarVolts.set(nowSolarVolts);
   displaySolarmA.set(nowSolarmA);

   batteryVoltsField->set(nowBatteryVolts);
   batterymAField->set(nowBatterymA);
   batterymAhField->set(batterymAh);
   solarVoltsField->set(nowSolarVolts);
   solarmAField->set(nowSolarmA);
   loadVoltsField->set(nowLoadVolts);
   loadmAField->set(nowLoadmA);
   loadmAhField->set(loadmAh);

   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Solar Monitor", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.setTextSize(2);

   Color DRAINING_COLOR = (Color)Color565::fromRGB(255, 100, 100);
   Color CHARGING_COLOR = (Color)Color565::fromRGB(100, 255, 100);
   feather.print("Battery: ", Color::HEADING2);
   if (displayBatterymA.get() >= 0)
   {
      feather.println("Charging", CHARGING_COLOR);
   }
   else if (displayBatterymA.get() < 0)
   {
      feather.println("Draining", DRAINING_COLOR);
   }
   feather.moveCursorY(2);

   feather.print("   Volts: ", Color::LABEL);
   feather.println(displayBatteryVolts.get(), voltsFormat, Color::VALUE);
   feather.moveCursorY(1);

   feather.print(" Current: ", Color::LABEL);
   feather.println(std::abs(displayBatterymA.get()), currentFormat, displayBatterymA.get() > 0 ? CHARGING_COLOR : DRAINING_COLOR);
   feather.moveCursorY(1);

   feather.print("     mAh:", Color::LABEL);
   feather.println(batterymAh, mAhFormat, batterymAh > 0 ? CHARGING_COLOR : DRAINING_COLOR);


   feather.moveCursorY(feather.charH());
   feather.print("Load", Color::HEADING2);
   feather.println();
   feather.moveCursorY(2);

   feather.print("   Volts: ", Color::LABEL);
   feather.println(displayLoadVolts.get(), voltsFormat, Color::VALUE);
   feather.moveCursorY(1);

   feather.print(" Current: ", Color::LABEL);
   feather.println(std::abs(displayLoadmA.get()), currentFormat, Color::VALUE);
   feather.moveCursorY(1);

   feather.print("     mAh: ", Color::LABEL);
   feather.println(loadmAh, mAhFormat2, Color::VALUE);


   feather.moveCursorY(feather.charH());
   feather.println("Solar", Color::HEADING2);
   feather.moveCursorY(2);

   feather.print("   Volts: ", Color::LABEL);
   feather.println(displaySolarVolts.get(), voltsFormat, Color::VALUE);
   feather.moveCursorY(1);

   feather.print(" Current: ", Color::LABEL);
   feather.println(displaySolarmA.get(), currentFormat, Color::VALUE);

   if (batteryPoint.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      if (batteryPoint.post(&client, true) == false)
      {
         Serial.println("InfluxDB write failed: ");
         Serial.println(client.getLastErrorMessage());
      }
      digitalWrite(BUILTIN_LED, LOW);
   }

   if (solarPoint.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      if (solarPoint.post(&client, true) == false)
      {
         Serial.println("InfluxDB write failed: ");
         Serial.println(client.getLastErrorMessage());
      }
      digitalWrite(BUILTIN_LED, LOW);
   }

   if (loadPoint.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      if (loadPoint.post(&client, true) == false)
      {
         Serial.println("InfluxDB write failed: ");
         Serial.println(client.getLastErrorMessage());
      }
      digitalWrite(BUILTIN_LED, LOW);
   }
}
