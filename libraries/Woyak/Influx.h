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

	/// <summary>
	/// Initializes time/Influx connection, printing progress through the given ArduinoBase
	/// (which prints to Serial and, on display-capable boards, the display as well). Assumes
	/// WiFi has already been initialized on the given board (e.g. via arduino.initWifi(...)).
	/// </summary>
	/// <param name="arduino">ArduinoBase instance that receives progress text</param>
	/// <returns>True when initialization succeeds</returns>
	bool begin(ArduinoBase& arduino)
	{
		if (WiFi.status() != WL_CONNECTED)
		{
			std::string message = std::string("WiFi connect failed: ") + WiFiX::statusString();
			arduino.println(message.c_str(), Color::RED);
			return false;
		}

		if (_status)
		{
			_status->setStatus(Status::WEB_CONNECTING);
		}

		if (!TimeSync::isSynced())
		{
			TimeSync::syncWithAutoTimezone("pool.ntp.org", "time.nis.gov");
		}

		bool success = _client.validateConnection();
		if (success)
		{
			if (_status)
			{
				_status->setStatus(Status::READY);
			}
			return true;
		}

		arduino.println(_client.getLastErrorMessage().c_str(), Color::RED);
		return false;
	}

	/// <summary>
	/// Convenience overload of begin(ArduinoBase&amp;) for callers that hold a pointer to
	/// an ArduinoBase (or derived, e.g. ArduinoWithDisplay) instance.
	/// </summary>
	/// <param name="arduino">Pointer to the ArduinoBase instance that receives progress text</param>
	/// <returns>True when initialization succeeds</returns>
	bool begin(ArduinoBase* arduino)
	{
		return begin(*arduino);
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

   /// <summary>Whether this field should be included when the owning point is posted.</summary>
   bool _enabled = true;

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
   /// Sets whether this field is included when the owning point is posted. Disabled fields
   /// are skipped entirely, regardless of their current value.
   /// </summary>
   /// <param name="enabled">True to include the field in future posts; false to omit it</param>
   void setEnabled(bool enabled)
   {
      _enabled = enabled;
   }

   /// <summary>
   /// Returns whether this field is currently included when the owning point is posted.
   /// </summary>
   /// <returns>True if the field is enabled</returns>
   bool isEnabled() const
   {
      return _enabled;
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

   /// <summary>
   /// Returns whether this field has collected at least one sample. Used to distinguish
   /// a genuine NaN/Inf value from simply not having warmed up yet.
   /// </summary>
   /// <returns>True if at least one sample has been recorded</returns>
   virtual bool hasData() = 0;
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

   /// <summary>
   /// Returns whether a value has been assigned yet.
   /// </summary>
   /// <returns>True if set() has been called at least once</returns>
   bool hasData() override
   {
      return !isnan(_value);
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

   /// <summary>
   /// Returns whether at least one sample has been recorded in the averaging window.
   /// </summary>
   /// <returns>True if at least one sample has been added</returns>
   bool hasData() override
   {
      return _stats.count() > 0;
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

   /// <summary>
   /// Returns whether at least one sample has been recorded in the rolling window.
   /// </summary>
   /// <returns>True if at least one sample has been added</returns>
   bool hasData() override
   {
      return _stats.count() > 0;
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
         Logger.log("InfluxDB write failed: client is null", LogSeverity::ERROR);
         return false;
      }

      // clear out the old values
      _point.clearFields();

      // populate new values, skipping disabled fields and invalid entries
      size_t validFieldCount = 0;
      size_t disabledCount = 0;
      size_t invalidCount = 0;
      size_t warmingUpCount = 0;
      for (InfluxField* field : _fields)
      {
         if (!field->isEnabled())
         {
            disabledCount++;
            continue;
         }

         const float value = field->get();
         if (std::isnan(value) || std::isinf(value))
         {
            if (field->hasData())
            {
               invalidCount++;
            }
            else
            {
               warmingUpCount++;
            }
            continue;
         }

         _point.addField(field->getName().c_str(), value, field->getDecimalPlaces());
         validFieldCount++;
      }

      if (validFieldCount == 0)
      {
         // If every missing field is simply still warming up (no samples collected yet)
         // and none are genuinely invalid, this is expected during startup - skip the
         // post quietly rather than logging it as an error.
         if (invalidCount == 0 && warmingUpCount > 0)
         {
            Logger.log("InfluxDB post skipped: fields still warming up (" +
               std::to_string(warmingUpCount) + " pending)");
            return false;
         }

         Logger.log("InfluxDB write failed: no valid field values to post (" +
            std::to_string(disabledCount) + " disabled, " + std::to_string(invalidCount) + " NaN/Inf, " +
            std::to_string(warmingUpCount) + " warming up)", LogSeverity::ERROR);
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

      Logger.log(std::string("InfluxDB write failed: ") + client->getLastErrorMessage().c_str(), LogSeverity::ERROR);
      return false;
   }
};
