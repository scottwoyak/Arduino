#pragma once

#include <WiFi.h>

#include "ArduinoWithDisplay.h"
#include "Feather.h"
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include "Status.h"
#include "TimedAverage.h"
#include "Timer.h"
#include "WiFiX.h"

constexpr auto TZ_INFO = "UTC-5";

/// <summary>
/// InfluxDB integration service that manages WiFi connectivity and Influx initialization.
/// </summary>
class Influx
{
private:
	const char* _wifiSSID;
	const char* _wifiPassword;
	InfluxDBClient* _client;
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
	{
		_wifiSSID = wifiSSID;
		_wifiPassword = wifiPassword;
		_client = client;
		_status = status;
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

		bool isConnected = WiFiX::connect(_wifiSSID, _wifiPassword, 10000);
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
			arduino->println("WiFi connect failed", Color::RED);
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
			Serial.println("WiFi connect failed");
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

//-------------------------------------------------------------------------------------------------
//
// Represents a named numeric field that can store or compute a value for an Influx point.
//
//-------------------------------------------------------------------------------------------------
class Field
{
private:
	std::string _name;
	uint8_t _decimalPlaces;

public:
	Field(const std::string& name, uint8_t decimalPlaces)
	{
	  _name = name;
	  _decimalPlaces = decimalPlaces;
	}

   virtual ~Field()
   {
   }

	const std::string& getName() const
	{
	  return _name;
	}

	uint8_t getDecimalPlaces() const
	{
	  return _decimalPlaces;
	}

   virtual void set(float value) = 0;

   virtual float get() = 0;
};

//-------------------------------------------------------------------------------------------------
//
// Stores the most recent value assigned to a field without any time-based aggregation.
//
//-------------------------------------------------------------------------------------------------
class ValueField : public Field
{
private:
	float _value = NAN;

public:
	ValueField(const std::string& name, uint8_t decimalPlaces) : Field(name, decimalPlaces)
	{
	}

	~ValueField() override = default;

	void set(float value) override
	{
		_value = value;
	}

	float get() override
	{
		return _value;
	}
};

//-------------------------------------------------------------------------------------------------
//
// Tracks a time-windowed average for a field using TimedAverage over the configured duration.
//
//-------------------------------------------------------------------------------------------------
class TimeAveragedField : public Field
{
private:
	TimedAverage _stats;

public:
	TimeAveragedField(float seconds, const std::string& name, uint8_t decimalPlaces)
		: Field(name, decimalPlaces),
		_stats(1000 * seconds)
	{
	}

	~TimeAveragedField() override = default;

	void set(float value) override
	{
		_stats.set(value);
	}

	float get() override
	{
		return _stats.average();
	}
};

//-------------------------------------------------------------------------------------------------
//
// Builds and posts an Influx point from a collection of field helpers and optional tags.
//
//-------------------------------------------------------------------------------------------------
class SimplePoint
{
private:
   Point _point;
   std::vector<Field*> _fields;

public:
   SimplePoint(const char* measurement) : _point(measurement)
   {
   }

	SimplePoint(const char* measurement, const std::vector<std::pair<const char*, const char*>>& tags) : _point(measurement)
	{
		for (const auto& tag : tags)
		{
			_point.addTag(tag.first, tag.second);
		}
	}

	~SimplePoint()
	{
		for (Field* field : _fields)
		{
			delete field;
		}
	}

   void addTag(const String& name, String value)
   {
	  _point.addTag(name, value);
   }

	Field* addValueField(const std::string& name, uint8_t decimalPlaces)
	{
	  Field* field = new ValueField(name, decimalPlaces);
	  _fields.push_back(field);
	  return field;
	}

	Field* addTimeAveragedField(float seconds, const std::string& name, uint8_t decimalPlaces)
	{
	  Field* field = new TimeAveragedField(seconds, name, decimalPlaces);
	  _fields.push_back(field);
	  return field;
	}

   bool post(InfluxDBClient* client, bool writeToSerial = false)
   {
	  // clear out the old values
	  _point.clearFields();

	  // populate new values
	  for (Field* field : _fields)
	  {
		 _point.addField(field->getName().c_str(), field->get(), field->getDecimalPlaces());
	  }

	  if (writeToSerial)
	  {
		 Serial.println(_point.toLineProtocol());
	  }

	  // send to Influx
	  return client->writePoint(_point);
   }

};
