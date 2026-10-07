#pragma once

#include "MonitorSketch.h"
#include "TempSensor.h"
#include "Timer.h"

///
/// <summary>
/// A MonitorSketch specialized for a single temperature/humidity sensor. Owns the
/// temperature, humidity, dew point, and absolute humidity time-averaged Influx
/// fields, samples the sensor, and reports
/// Temp_Monitor (headless) and Temp_Monitor_Display (with display); the display
/// sketch layers its own rendering on top of the values this class already tracks.
/// </summary>
///
class TempMonitorSketch : public MonitorSketch
{
public:
   /// <summary>How often, in seconds, the time-averaged Influx fields are uploaded (and the averaging period of those fields).</summary>
   static constexpr uint8_t INFLUX_INTERVAL_S = 60;

   /// <summary>Decimal places used when uploading/reporting temperature and dew point.</summary>
   static constexpr uint8_t INFLUX_TEMP_DECIMAL_PLACES = 3;

   /// <summary>Decimal places used when uploading/reporting humidity and absolute humidity.</summary>
   static constexpr uint8_t INFLUX_HUMIDITY_DECIMAL_PLACES = 2;

private:
   /// <summary>How often, in milliseconds, updateReadings() samples the sensor.</summary>
   static constexpr uint16_t SENSOR_INTERVAL_MS = 5000;

   TempSensor _sensor;
    TimerSecs _uploadTimer{ INFLUX_INTERVAL_S };
   Timer _sensorTimer{ SENSOR_INTERVAL_MS };

   InfluxField* _tempField = nullptr;
   InfluxField* _humField = nullptr;
   InfluxField* _dewPointField = nullptr;
   InfluxField* _absoluteHumidityField = nullptr;

   /// <summary>The single instance, used by the static bridge functions _onHealth()/_initSensor()/_sensorTypeLabel() to reach the instance (addSensor()/DeviceHealth::extraFields only accept captureless function pointers).</summary>
   inline static TempMonitorSketch* _tempInstance = nullptr;

   ///
   /// <summary>
   /// Initializes the sensor. Registered with addSensor() from begin() since
   /// addSensor() only accepts a captureless function pointer.
   /// </summary>
   /// <returns>True if the sensor initialized successfully.</returns>
   ///
   static bool _initSensor()
   {
      return _tempInstance->_sensor.begin(false, true);
   }

   ///
   /// <summary>
   /// Returns the sensor's type label. Registered with addSensor() from begin() since
   /// addSensor() only accepts a captureless function pointer.
   /// </summary>
   /// <returns>The sensor's type label.</returns>
   ///
   static const char* _sensorTypeLabel()
   {
      return _tempInstance->_sensor.type();
   }

   ///
   /// <summary>
   /// Adds the current sensor field values to the health JSON. Registered with
   /// DeviceHealth::extraFields from begin() since it only accepts a captureless function
   /// pointer.
   /// </summary>
   /// <param name="doc">The health document to add fields to.</param>
   ///
   static void _onHealth(JsonDocument* doc)
   {
      _tempInstance->_addToHealth(doc);
   }

   ///
   /// <summary>
   /// Adds this instance's current field values to the health JSON.
   /// </summary>
   /// <param name="doc">The health document to add fields to.</param>
   ///
   void _addToHealth(JsonDocument* doc)
   {
      (*doc)["temperature"] = temp();
      if (_sensor.supportsHumidity())
      {
         (*doc)["humidity"] = humidity();
         (*doc)["dewPoint"] = dewPoint();
         (*doc)["absoluteHumidity"] = absoluteHumidity();
      }
   }

public:
   ///
   /// <summary>
   /// Creates a TempMonitorSketch bound to the given board. Register any extra loop
   /// hooks afterward, then call begin(); the temp/humidity sensor itself is owned and
   /// registered internally. SketchConfig's enableOTA/enableRebooter and InfluxConfig's
   /// post interval/prompt-for-context/CPU temp are identical across all
   /// TempMonitorSketch-based sketches, so they are fixed internally rather than passed in.
   /// </summary>
   /// <param name="sketchName">Sketch name, printed at boot and used as the OTA update identifier.</param>
   /// <param name="version">Sketch version string, printed at boot and used for OTA update checks.</param>
   /// <param name="preferencesNamespace">Preferences (NVS) namespace used to persist the prompted Influx site/location.</param>
   ///
   TempMonitorSketch(const char* sketchName, const char* version, const char* preferencesNamespace)
      : MonitorSketch(
                    SketchConfig{ .sketchName = sketchName, .version = version, .preferencesNamespace = preferencesNamespace, .cpuFrequencyMhz = 80, .enableOTA = true },
                    InfluxConfig{ .promptForContext = true, .includeCpuTemp = true })
           {
      ASSERT(_tempInstance == nullptr);

      _tempInstance = this;
   }

   ///
   /// <summary>
   /// Registers the sensor init hook, then runs MonitorSketch::begin() and registers
   /// the temperature/humidity/dew point/absolute humidity time-averaged Influx fields
   /// and the health hook that reports them.
   /// </summary>
   ///
   void begin()
   {
      // Fall back to the internal ESP32 CPU temperature sensor if no external sensor is
      // found, so the device still reports a (less accurate) temperature reading instead
      // of failing to start. Registered before MonitorSketch::begin() so the sensor is
      // initialized before WiFi/Influx setup. Uses static bridge functions rather than
      // capturing lambdas since addSensor() only accepts captureless function pointers.
      addSensor("Sensor", _initSensor, _sensorTypeLabel);

      MonitorSketch::begin();
      DeviceHealth::extraFields = _onHealth;

      InfluxPoint* point = addPoint({ { "item", "Sensor" } });
      _tempField = point->addTimeAverageField(INFLUX_INTERVAL_S, "temperature", INFLUX_TEMP_DECIMAL_PLACES);
      _humField = point->addTimeAverageField(INFLUX_INTERVAL_S, "humidity", INFLUX_HUMIDITY_DECIMAL_PLACES);
      _dewPointField = point->addTimeAverageField(INFLUX_INTERVAL_S, "dewPoint", INFLUX_TEMP_DECIMAL_PLACES);
      _absoluteHumidityField = point->addTimeAverageField(INFLUX_INTERVAL_S, "absoluteHumidity", INFLUX_HUMIDITY_DECIMAL_PLACES);
   }

   ///
   /// <summary>
   /// Runs MonitorSketch::loop(), then samples the sensor and updates the
   /// time-averaged Influx fields, once per sensorIntervalMs. Call once per loop().
   /// </summary>
   ///
   void loop()
   {
      MonitorSketch::loop();

      if (_uploadTimer.ready())
      {
         postPoints();
      }

      if (!_sensorTimer.ready())
      {
         return;
      }

      Readings readings = _sensor.readAll();
      _tempField->set(readings.tempF);
      _humField->set(readings.humidity);
      _dewPointField->set(readings.dewPointF);
      _absoluteHumidityField->set(readings.absoluteHumidity);
   }

   /// <summary>Current time-averaged temperature, in Fahrenheit.</summary>
   float temp() const { return _tempField->get(); }

   /// <summary>Current time-averaged relative humidity, 0-100%.</summary>
   float humidity() const { return _humField->get(); }

   /// <summary>Current time-averaged dew point, in Fahrenheit.</summary>
   float dewPoint() const { return _dewPointField->get(); }

   /// <summary>Current time-averaged absolute humidity, in grams per cubic meter.</summary>
   float absoluteHumidity() const { return _absoluteHumidityField->get(); }

   /// <summary>The underlying sensor, for callers that need type()/supportsHumidity()/etc.</summary>
   TempSensor& sensor() { return _sensor; }
};
