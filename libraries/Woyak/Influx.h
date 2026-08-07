#pragma once

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <cmath>
#include "ArduinoBase.h"
#include "RollingAverage.h"
#include "Status.h"
#include "TimedAverage.h"
#include "Timer.h"
#include "TimeSync.h"
#include "WiFiSettings.h"
#include "WiFiX.h"

// Display-based methods (and ArduinoWithDisplay.h, which requires a board-specific
// LGFX type) are only available on boards with a display. ARDUINO_DISPLAY_SUPPORTED
// is defined by ArduinoBoard.h when the target board has one.
#ifdef ARDUINO_DISPLAY_SUPPORTED
#include "ArduinoWithDisplay.h"
#endif

constexpr auto TZ_INFO = "UTC-5";

///
/// <summary>
/// InfluxDB integration service that manages Influx initialization. WiFi connectivity is
/// owned by the ArduinoBase instance passed in to the begin(...)/connectWiFi() methods.
/// </summary>
///
class Influx
{
private:
	/// <summary>InfluxDB client used for time sync validation and data writes.</summary>
	InfluxDBClient _client;

	/// <summary>Optional status LED indicator; nullptr if not used.</summary>
	IStatus* _status;

	/// <summary>Timer controlling how often uploads should occur, per ready().</summary>
	Timer _uploadTimer;

public:
	/// <summary>
	/// Creates an Influx service with an internally-owned InfluxDB client and optional status
	/// indicator. The client connection parameters default to the values in WiFiSettings.h.
	/// </summary>
	/// <param name="uploadIntervalSecs">Seconds between uploads, as reported by ready()</param>
	/// <param name="status">Optional status indicator instance</param>
	/// <param name="url">InfluxDB server URL</param>
	/// <param name="org">InfluxDB organization</param>
	/// <param name="bucket">InfluxDB bucket</param>
	/// <param name="token">InfluxDB access token</param>
	/// <param name="certInfo">Server certificate info</param>
	Influx(uint16_t uploadIntervalSecs,
			 IStatus* status = nullptr,
			 const char* url = INFLUXDB_URL,
			 const char* org = INFLUXDB_ORG,
			 const char* bucket = INFLUXDB_BUCKET,
			 const char* token = INFLUXDB_TOKEN,
			 const char* certInfo = InfluxDbCloud2CACert) :
		_client(url, org, bucket, token, certInfo),
		_uploadTimer(uploadIntervalSecs * 1000UL)
	{
		_status = status;
	}

	/// <summary>
	/// Returns the internally-owned InfluxDB client, e.g. for InfluxPoint::post().
	/// </summary>
	/// <returns>Pointer to the InfluxDB client</returns>
	InfluxDBClient* client()
	{
		return &_client;
	}

	/// <summary>
	/// Returns true when the upload interval has elapsed, indicating it's time to post data.
	/// </summary>
	/// <returns>True if an upload should occur now</returns>
	bool ready()
	{
		return _uploadTimer.ready();
	}

	#ifdef ARDUINO_DISPLAY_SUPPORTED
	/// <summary>
	/// Initializes time/Influx connection with display progress output. Assumes WiFi has
	/// already been initialized on the given board (e.g. via arduino->initWifi(...)).
	/// </summary>
	/// <param name="arduino">Display-capable Arduino wrapper used for progress UI</param>
	/// <param name="printDiagnostics">True to print time sync progress/result to Serial</param>
	/// <returns>True when initialization succeeds</returns>
	bool begin(ArduinoWithDisplay* arduino, bool printDiagnostics = false)
	{
		if (WiFi.status() != WL_CONNECTED)
		{
			arduino->println(String("WiFi connect failed: ") + WiFiX::statusString(), Color::RED);
			return false;
		}

		if (_status)
		{
			_status->setStatus(Status::WEB_CONNECTING);
		}

		arduino->print("Syncing Time... ", Color::LABEL);
		TimeSync::sync(TZ_INFO, "pool.ntp.org", "time.nis.gov", nullptr, printDiagnostics);
		arduino->printlnR("ok", Color::VALUE);

		arduino->print("Influx... ", Color::LABEL);
		if (_client.validateConnection())
		{
			arduino->printlnR("ok", Color::VALUE);
			if (printDiagnostics)
			{
				Serial.println(_client.getServerUrl());
			}
			if (_status)
			{
				_status->setStatus(Status::READY);
			}
			return true;
		}

		arduino->printlnR("FAILED", Color::RED);
		arduino->println(_client.getLastErrorMessage(), Color::RED);
		return false;
	}
#endif

	/// <summary>
	/// Initializes time/Influx connection, printing progress through the given ArduinoBase
	/// (which prints to Serial and, on display-capable boards, the display as well). Assumes
	/// WiFi has already been initialized on the given board (e.g. via arduino.initWifi(...)).
	/// </summary>
	/// <param name="arduino">ArduinoBase instance that receives progress text</param>
	/// <param name="printDiagnostics">True to print time sync progress/result to Serial</param>
	/// <returns>True when initialization succeeds</returns>
	bool begin(ArduinoBase& arduino, bool printDiagnostics = false)
	{
		if (WiFi.status() != WL_CONNECTED)
		{
			arduino.println((String("WiFi connect failed: ") + WiFiX::statusString()).c_str(), Color::RED);
			return false;
		}

		if (_status)
		{
			_status->setStatus(Status::WEB_CONNECTING);
		}

		arduino.print("Syncing Time...", Color::LABEL);
		TimeSync::sync(TZ_INFO, "pool.ntp.org", "time.nis.gov", nullptr, printDiagnostics);
		arduino.printlnR("OK", Color::VALUE);

		arduino.print("Influx...", Color::LABEL);
		if (_client.validateConnection())
		{
			arduino.printlnR("OK", Color::VALUE);
			if (printDiagnostics)
			{
				Serial.println(_client.getServerUrl());
			}
			if (_status)
			{
				_status->setStatus(Status::READY);
			}
			return true;
		}

		arduino.printlnR("FAILED", Color::RED);
		arduino.println(_client.getLastErrorMessage().c_str(), Color::RED);
		return false;
	}

	/// <summary>
	/// Displays common initialization header content on display-equipped sketches.
	/// </summary>
	/// <param name="arduino">Display-capable Arduino wrapper</param>
	/// <returns>None</returns>
#ifdef ARDUINO_DISPLAY_SUPPORTED
	static void startInit(ArduinoWithDisplay* arduino)
	{
		arduino->clearDisplay();
		arduino->setTextSize(2);
		arduino->println("Initializing", Color::HEADING);
		arduino->moveCursorY(arduino->charH() / 2);
	}

	/// <summary>
	/// Restores display/serial state after initialization UI.
	/// </summary>
	/// <param name="arduino">Display-capable Arduino wrapper</param>
	/// <returns>None</returns>
	static void endInit(ArduinoWithDisplay* arduino)
	{
		arduino->clearDisplay();
	}
#endif
};

///
/// <summary>
/// Abstract named numeric field that can store or compute a value for an Influx point.
/// </summary>
///
class InfluxField
{
private:
   /// <summary>Field name used as the Influx field key.</summary>
   std::string _name;

   /// <summary>Number of decimal places used when writing the value to Influx.</summary>
   uint8_t _decimalPlaces;

public:
   /// <summary>
   /// Creates a field with a name and decimal place precision.
   /// </summary>
   /// <param name="name">Field name</param>
   /// <param name="decimalPlaces">Number of decimal places for the value</param>
   InfluxField(const std::string& name, uint8_t decimalPlaces)
   {
      _name = name;
      _decimalPlaces = decimalPlaces;
   }

   virtual ~InfluxField() = default;

   /// <summary>
   /// Returns the field name.
   /// </summary>
   /// <returns>Field name string</returns>
   const std::string& getName() const
   {
      return _name;
   }

   /// <summary>
   /// Returns the number of decimal places for this field.
   /// </summary>
   /// <returns>Decimal place count</returns>
   uint8_t getDecimalPlaces() const
   {
      return _decimalPlaces;
   }

   /// <summary>
   /// Sets the current value of the field.
   /// </summary>
   /// <param name="value">Value to set</param>
   virtual void set(float value) = 0;

   /// <summary>
   /// Returns the current value of the field.
   /// </summary>
   /// <returns>Current field value</returns>
   virtual float get() = 0;
};

///
/// <summary>
/// Field that stores the most recent assigned value without time-based aggregation.
/// </summary>
///
class InfluxValueField : public InfluxField
{
private:
   /// <summary>Most recently assigned value.</summary>
   float _value = NAN;

public:
   /// <summary>
   /// Creates an InfluxValueField with a name and decimal place precision.
   /// </summary>
   /// <param name="name">Field name</param>
   /// <param name="decimalPlaces">Number of decimal places for the value</param>
   InfluxValueField(const std::string& name, uint8_t decimalPlaces) : InfluxField(name, decimalPlaces)
   {
   }

   ~InfluxValueField() override = default;

   /// <summary>
   /// Sets the current field value.
   /// </summary>
   /// <param name="value">Value to assign</param>
   void set(float value) override
   {
      _value = value;
   }

   /// <summary>
   /// Returns the current field value.
   /// </summary>
   /// <returns>Most recently assigned value</returns>
   float get() override
   {
      return _value;
   }
};

///
/// <summary>
/// Field that tracks a time-windowed average using TimedAverage over the configured duration.
/// </summary>
///
class InfluxTimeAveragedField : public InfluxField
{
private:
   /// <summary>Rolling time-windowed average accumulator.</summary>
   TimedAverage _stats;

public:
   /// <summary>
   /// Creates an InfluxTimeAveragedField with a time window, name, and decimal place precision.
   /// </summary>
   /// <param name="seconds">Duration of the averaging window in seconds</param>
   /// <param name="name">Field name</param>
   /// <param name="decimalPlaces">Number of decimal places for the value</param>
   InfluxTimeAveragedField(float seconds, const std::string& name, uint8_t decimalPlaces)
      : InfluxField(name, decimalPlaces),
        _stats(1000 * seconds)
   {
   }

   ~InfluxTimeAveragedField() override = default;

   /// <summary>
   /// Adds a sample to the rolling time-averaged field.
   /// </summary>
   /// <param name="value">Sample value to add</param>
   void set(float value) override
   {
      _stats.set(value);
   }

   /// <summary>
   /// Returns the current rolling average value.
   /// </summary>
   /// <returns>Current average over the configured time window</returns>
   float get() override
   {
      return _stats.average();
   }
};

///
/// <summary>
/// Field that tracks a rolling average over a fixed number of samples using RollingAverage.
/// </summary>
///
class InfluxRollingAverageField : public InfluxField
{
private:
   /// <summary>Rolling sample-count average accumulator.</summary>
   RollingAverage _stats;

public:
   /// <summary>
   /// Creates an InfluxRollingAverageField with a sample window, name, and decimal place precision.
   /// </summary>
   /// <param name="size">Number of samples retained in the rolling window</param>
   /// <param name="name">Field name</param>
   /// <param name="decimalPlaces">Number of decimal places for the value</param>
   InfluxRollingAverageField(size_t size, const std::string& name, uint8_t decimalPlaces)
      : InfluxField(name, decimalPlaces),
        _stats(size)
   {
   }

   ~InfluxRollingAverageField() override = default;

   /// <summary>
   /// Adds a sample to the rolling average field.
   /// </summary>
   /// <param name="value">Sample value to add</param>
   void set(float value) override
   {
      _stats.set(value);
   }

   /// <summary>
   /// Returns the current rolling average value.
   /// </summary>
   /// <returns>Current average over the configured sample window</returns>
   float get() override
   {
      return _stats.average();
   }
};

///
/// <summary>
/// Builds and posts an Influx point from a collection of field helpers and optional tags.
/// </summary>
///
class InfluxPoint
{
private:
   /// <summary>Underlying Influx point being populated and posted.</summary>
   Point _point;

   /// <summary>Owned collection of fields contributing values to each post.</summary>
   std::vector<InfluxField*> _fields;

   InfluxPoint(const InfluxPoint&) = delete;
   InfluxPoint& operator=(const InfluxPoint&) = delete;

public:
   /// <summary>
   /// Creates an InfluxPoint for the given measurement name.
   /// </summary>
   /// <param name="measurement">Influx measurement name</param>
   InfluxPoint(const char* measurement) : _point(measurement)
   {
   }

   /// <summary>
   /// Creates an InfluxPoint for the given measurement name with initial tags.
   /// </summary>
   /// <param name="measurement">Influx measurement name</param>
   /// <param name="tags">Key/value pairs to attach as Influx tags</param>
   InfluxPoint(const char* measurement, const std::vector<std::pair<const char*, const char*>>& tags) : _point(measurement)
   {
      for (const auto& tag : tags)
      {
         _point.addTag(tag.first, tag.second);
      }
   }

   ~InfluxPoint()
   {
      for (InfluxField* field : _fields)
      {
         delete field;
      }
   }

   /// <summary>
   /// Adds a tag to the Influx point.
   /// </summary>
   /// <param name="name">Tag name</param>
   /// <param name="value">Tag value</param>
   void addTag(const String& name, const String& value)
   {
      _point.addTag(name, value);
   }

   /// <summary>
   /// Adds a value field that stores the most recent assigned value.
   /// </summary>
   /// <param name="name">Field name</param>
   /// <param name="decimalPlaces">Number of decimal places for the value</param>
   /// <returns>Pointer to the created field</returns>
   InfluxField* addValueField(const std::string& name, uint8_t decimalPlaces)
   {
      InfluxField* field = new InfluxValueField(name, decimalPlaces);
      _fields.push_back(field);
      return field;
   }

   /// <summary>
   /// Adds a time-averaged field that accumulates a rolling average over the given window.
   /// </summary>
   /// <param name="seconds">Duration of the averaging window in seconds</param>
   /// <param name="name">Field name</param>
   /// <param name="decimalPlaces">Number of decimal places for the value</param>
   /// <returns>Pointer to the created field</returns>
   InfluxField* addTimeAverageField(float seconds, const std::string& name, uint8_t decimalPlaces)
   {
      InfluxField* field = new InfluxTimeAveragedField(seconds, name, decimalPlaces);
      _fields.push_back(field);
      return field;
   }

   /// <summary>
   /// Adds a rolling average field that accumulates an average over a fixed number of samples.
   /// </summary>
   /// <param name="size">Number of samples retained in the rolling window</param>
   /// <param name="name">Field name</param>
   /// <param name="decimalPlaces">Number of decimal places for the value</param>
   /// <returns>Pointer to the created field</returns>
   InfluxField* addRollingAverageField(size_t size, const std::string& name, uint8_t decimalPlaces)
   {
      InfluxField* field = new InfluxRollingAverageField(size, name, decimalPlaces);
      _fields.push_back(field);
      return field;
   }

   /// <summary>
   /// Populates and posts the Influx point with current field values.
   /// </summary>
   /// <param name="client">InfluxDB client to write to</param>
   /// <param name="writeToSerial">If true, also prints the line protocol to Serial</param>
   /// <returns>True if the write succeeded; otherwise false</returns>
   bool post(InfluxDBClient* client, bool writeToSerial = false)
   {
      if (client == nullptr)
      {
         Serial.println("InfluxDB write failed: client is null");
         return false;
      }

      // clear out the old values
      _point.clearFields();

      // populate new values, skipping invalid entries
      size_t validFieldCount = 0;
      for (InfluxField* field : _fields)
      {
         const float value = field->get();
         if (std::isnan(value) || std::isinf(value))
         {
            continue;
         }

         _point.addField(field->getName().c_str(), value, field->getDecimalPlaces());
         validFieldCount++;
      }

      if (validFieldCount == 0)
      {
         Serial.println("InfluxDB write failed: no valid field values to post");
         return false;
      }

      if (writeToSerial)
      {
         Serial.println(_point.toLineProtocol());
      }

      if (client->writePoint(_point))
      {
         return true;
      }

      Serial.print("InfluxDB write failed: ");
      Serial.println(client->getLastErrorMessage());
      return false;
   }
};
