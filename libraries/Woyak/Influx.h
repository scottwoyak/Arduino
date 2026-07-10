#pragma once

#include "ArduinoWithDisplay.h"
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <cmath>
#include "Status.h"
#include "TimedAverage.h"
#include "Timer.h"
#include "WiFiX.h"

constexpr auto TZ_INFO = "UTC-5";

///
/// <summary>
/// InfluxDB integration service that manages WiFi connectivity and Influx initialization.
/// </summary>
///
class Influx
{
private:
	/// <summary>WiFi connection manager initialized with the access point credentials.</summary>
	WiFiX _wifiX;

	/// <summary>InfluxDB client used for time sync validation and data writes.</summary>
	InfluxDBClient* _client;

	/// <summary>Optional status LED indicator; nullptr if not used.</summary>
	IStatus* _status;

public:
	/// <summary>
	/// Creates an Influx service with credentials, client, and optional status indicator.
	/// </summary>
	/// <param name="wifiSSID">WiFi network SSID</param>
	/// <param name="wifiPassword">WiFi network password</param>
	/// <param name="client">InfluxDB client instance</param>
	/// <param name="status">Optional status indicator instance</param>
	Influx(const char* wifiSSID, const char* wifiPassword, InfluxDBClient* client, IStatus* status = nullptr)
		: _wifiX(wifiSSID, wifiPassword)
	{
		_client = client;
		_status = status;

		// Keep the TLS connection open between writes instead of doing a full TLS
		// handshake + teardown every upload cycle. Repeated non-reused BearSSL/mbedTLS
		// handshakes on ESP32 leak/fragment heap over time (observed free heap dropping
		// from ~196KB to ~56KB within a few minutes), eventually causing new handshakes
		// to fail outright with "connection refused".
		_client->setHTTPOptions(HTTPOptions().connectionReuse(true));
	}

	/// <summary>
	/// Attempts a WiFi connection using configured credentials.
	/// </summary>
	/// <returns>True when connected, otherwise false</returns>
	bool connectWiFi()
	{
		if (WiFi.status() == WL_CONNECTED)
		{
			return true;
		}

		if (_status)
		{
			_status->setStatus(Status::WIFI_CONNECTING);
		}

		bool isConnected = _wifiX.connect();
		if (_status && isConnected)
		{
			_status->setStatus(Status::READY);
		}

		return isConnected;
	}

	/// <summary>
	/// Ensures WiFi is connected, reconnecting when needed.
	/// </summary>
	/// <returns>True when connected, otherwise false</returns>
	bool ensureWiFiConnected()
	{
		if (WiFi.status() == WL_CONNECTED)
		{
			return true;
		}

		return connectWiFi();
	}

	/// <summary>
	/// Initializes WiFi/time/Influx connection with display progress output.
	/// </summary>
	/// <param name="arduino">Display-capable Arduino wrapper used for progress UI</param>
	/// <returns>True when initialization succeeds</returns>
	bool begin(ArduinoWithDisplay* arduino)
	{
		arduino->print("WiFi... ", Color::LABEL);
		if (!connectWiFi())
		{
			arduino->printlnR("FAILED", Color::RED);
			arduino->println(String("WiFi connect failed: ") + WiFiX::statusString(), Color::RED);
			return false;
		}
		arduino->printlnR("ok", Color::VALUE);

		if (_status)
		{
			_status->setStatus(Status::WEB_CONNECTING);
		}

		bool oldEcho = arduino->echoToSerial;
		arduino->echoToSerial = false;
		arduino->print("Syncing Time... ", Color::LABEL);
		timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
		arduino->printlnR("ok", Color::VALUE);
		arduino->echoToSerial = oldEcho;

		arduino->print("Influx... ", Color::LABEL);
		if (_client->validateConnection())
		{
			arduino->printlnR("ok", Color::VALUE);
			Serial.println(_client->getServerUrl());
			if (_status)
			{
				_status->setStatus(Status::READY);
			}
			return true;
		}

		arduino->printlnR("FAILED", Color::RED);
		arduino->println(_client->getLastErrorMessage(), Color::RED);
		return false;
	}

	/// <summary>
	/// Initializes WiFi/time/Influx connection with Serial progress output.
	/// </summary>
	/// <returns>True when initialization succeeds</returns>
	bool begin()
	{
		Serial.println("WiFi... ");
		if (!connectWiFi())
		{
			Serial.println("FAILED");
			Serial.print("WiFi connect failed: ");
			Serial.println(WiFiX::statusString());
			return false;
		}
		Serial.println("ok");

		if (_status)
		{
			_status->setStatus(Status::WEB_CONNECTING);
		}

		Serial.print("Syncing Time... ");
		timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
		Serial.println("ok");

		Serial.print("Influx... ");
		if (_client->validateConnection())
		{
			Serial.println("ok");
			Serial.println(_client->getServerUrl());
			if (_status)
			{
				_status->setStatus(Status::READY);
			}
			return true;
		}

		Serial.println("FAILED");
		Serial.println(_client->getLastErrorMessage());
		return false;
	}

	/// <summary>
	/// Displays common initialization header content on display-equipped sketches.
	/// </summary>
	/// <param name="arduino">Display-capable Arduino wrapper</param>
	/// <returns>None</returns>
	static void startInit(ArduinoWithDisplay* arduino)
	{
		arduino->echoToSerial = true;
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
		arduino->echoToSerial = false;
	}
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
   InfluxField* addTimeAveragedField(float seconds, const std::string& name, uint8_t decimalPlaces)
   {
      InfluxField* field = new InfluxTimeAveragedField(seconds, name, decimalPlaces);
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
