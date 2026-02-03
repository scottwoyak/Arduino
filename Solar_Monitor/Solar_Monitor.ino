
#include "Feather.h"
#include "Adafruit_INA219.h"
#include "SerialX.h"
#include "TimedAverager.h"
#include "Influx.h"
#include "WiFiSettings.h"
#include "Util.h"

Feather feather;
Adafruit_INA219 sensor1(0x40);
Adafruit_INA219 sensor2(0x41); // A0 shorted

Format voltsFormat("#.## v");
Format currentFormat("####.# mA");
Format percentFormat("###.#%");
Format mAhFormat("+####.# mAh");

TimedAverager displayBatteryVolts(1000);
TimedAverager displayBatterymA(1000);
TimedAverager displaySolarVolts(1000);
TimedAverager displaySolarmA(1000);

constexpr auto INFLUX_MEASUREMENT = "Power";
constexpr auto INFLUX_INTERVAL_S = 10;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

TimedPoint batteryPoint(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT, { {"item", "Battery"} });
TimedPoint solarPoint(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT, { { "item", "Solar" } });
Field* batteryVoltsField = batteryPoint.addTimeAveragedField("volts", 3);
Field* batterymAField = batteryPoint.addTimeAveragedField("mA", 1);
Field* batterymAhField = batteryPoint.addTimeAveragedField("mAh", 1);
Field* solarVoltsField = solarPoint.addTimeAveragedField("volts", 3);
Field* solarmAField = solarPoint.addTimeAveragedField("mA", 1);

void setup()
{
   feather.begin();
   SerialX::begin();

   feather.echoToSerial = true;
   feather.clearDisplay();
   feather.println("Initializing", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.print("Battery INA219... ", Color::LABEL);
   if (!sensor1.begin())
   {
      Serial.println("Battery INA219 Not Found");
      feather.setTextSize(3);
      feather.display.fillScreen((uint16_t)Color::RED);
      feather.setCursor(0, feather.display.height() / 2 - feather.charH());
      feather.printlnC("Battery INA219", Color::WHITE, Color::RED);
      feather.printlnC("Not Found", Color::WHITE, Color::RED);
      while (1);
   }
   feather.printR("ok", Color::VALUE);

   feather.print("Solar INA219... ", Color::LABEL);
   if (!sensor2.begin())
   {
      Serial.println("INA219 (2) Not Found");
      feather.setTextSize(3);
      feather.display.fillScreen((uint16_t)Color::RED);
      feather.setCursor(0, feather.display.height() / 2 - feather.charH());
      feather.printlnC("Solar INA219", Color::WHITE, Color::RED);
      feather.printlnC("Not Found", Color::WHITE, Color::RED);
      while (1);
   }
   feather.printR("ok", Color::VALUE);

   Influx::begin(&feather, WIFI_SSID, WIFI_PASSWORD, &client);

   feather.clearDisplay();
   feather.echoToSerial = false;
}

unsigned long lastMicros = 0;
double mAh = 0;

void loop()
{
   if (feather.buttonA.wasPressed())
   {
      mAh = 0;
   }

   feather.setCursor(0, 0);
   feather.setTextSize(2);

   float nowBatteryVolts = sensor1.getBusVoltage_V() + sensor1.getShuntVoltage_mV() / 1000;
   float nowSolarVolts = sensor2.getBusVoltage_V() + sensor2.getShuntVoltage_mV() / 1000;
   float nowBatterymA = sensor1.getCurrent_mA();
   float nowSolarmA = sensor2.getCurrent_mA();

   unsigned long now = micros();
   if (lastMicros != 0)
   {
      double hours = (now - lastMicros) / (60.0 * 60 * 1000.0 * 1000);
      mAh += (nowBatterymA * hours);
   }
   lastMicros = now;


   displayBatteryVolts.set(nowBatteryVolts);
   displayBatterymA.set(nowBatterymA);
   displaySolarVolts.set(nowSolarVolts);
   displaySolarmA.set(nowSolarmA);

   batteryVoltsField->set(nowBatteryVolts);
   batterymAField->set(nowBatterymA);
   batterymAhField->set(mAh);
   solarVoltsField->set(nowSolarVolts);
   solarmAField->set(nowSolarmA);

   constexpr Color DRAINING_COLOR = (Color)Util::to565(255, 150, 150);
   constexpr Color CHARGING_COLOR = (Color)Util::to565(100, 255, 100);
   feather.print(" Battery: ", Color::HEADING2);
   feather.print("Charging", displayBatterymA.get() > 0 ? CHARGING_COLOR : DRAINING_COLOR);
   feather.println();
   feather.moveCursorY(2);

   feather.print("   Volts: ", Color::LABEL);
   feather.println(displayBatteryVolts.get(), voltsFormat, Color::VALUE);
   feather.moveCursorY(1);

   feather.print(" Current: ", Color::LABEL);
   feather.println(std::abs(displayBatterymA.get()), currentFormat, displayBatterymA.get() > 0 ? CHARGING_COLOR : DRAINING_COLOR);
   feather.moveCursorY(1);

   feather.print("     mAh:", Color::LABEL);
   feather.println(mAh, mAhFormat, mAh > 0 ? CHARGING_COLOR : DRAINING_COLOR);


   feather.moveCursorY(feather.charH() / 2);
   feather.println("   Solar: ", Color::HEADING2);
   feather.moveCursorY(2);

   feather.print("   Volts: ", Color::LABEL);
   feather.println(displaySolarVolts.get(), voltsFormat, Color::VALUE);
   feather.moveCursorY(1);

   feather.print(" Current: ", Color::LABEL);
   feather.println(displaySolarmA.get(), currentFormat, Color::VALUE);

   if (batteryPoint.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      if (batteryPoint.post(&client) == false)
      {
         Serial.println("InfluxDB write failed: ");
         Serial.println(client.getLastErrorMessage());
      }
      digitalWrite(BUILTIN_LED, LOW);
   }

   if (solarPoint.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      if (solarPoint.post(&client) == false)
      {
         Serial.println("InfluxDB write failed: ");
         Serial.println(client.getLastErrorMessage());
      }
      digitalWrite(BUILTIN_LED, LOW);
   }
}
