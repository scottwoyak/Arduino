#pragma once

#include <WiFi.h>

#include "ArduinoWithDisplay.h"
#include "Feather.h"
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include "Status.h"
#include "TimedStats.h"

constexpr auto TZ_INFO = "UTC-5";

//-------------------------------------------------------------------------------------------------
//
// General Influx functions
//
//-------------------------------------------------------------------------------------------------
namespace Influx
{
   void begin(
	  ArduinoWithDisplay* arduino,
	  const char* wifiSSID,
	  const char* wifiPassword,
	  InfluxDBClient* client,
	  IStatus* status = nullptr
   )
	{
	  arduino->print("WiFi... ", Color::LABEL);
	  if (status)
	  {
		 status->setStatus(Status::WIFI_CONNECTING);
	  }

	  // Setup wifi
	  WiFi.mode(WIFI_STA);
	  WiFi.begin(wifiSSID, wifiPassword);

	  while (WiFi.status() != WL_CONNECTED)
	  {
		 Serial.print(".");
		 delay(100);
	  }
	  arduino->printlnR("ok", Color::VALUE);

	  if (status)
	  {
		 status->setStatus(Status::WEB_CONNECTING);
	  }

	  // Accurate time is necessary for certificate validation and writing in batches
	  // We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
	  // Syncing progress and the time will be printed to Serial.
	  bool oldEcho = arduino->echoToSerial;
	  arduino->echoToSerial = false; // timeSync prints to Serial
	  arduino->print("Syncing Time... ", Color::LABEL);
	  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
	  arduino->printlnR("ok", Color::VALUE);
	  arduino->echoToSerial = oldEcho;

	  // Check server connection
	  arduino->print("Influx... ", Color::LABEL);

	  if (client->validateConnection())
	  {
		 arduino->printlnR("ok", Color::VALUE);
		 Serial.println(client->getServerUrl());
		 if (status)
		 {
			status->setStatus(Status::READY);
		 }
	  }
	  else
	  {
		 arduino->printlnR("FAILED", Color::RED);
		 arduino->println(client->getLastErrorMessage(), Color::RED);
		 while (1);
	  }
   }

   void begin(
	  const char* wifiSSID, 
	  const char* wifiPassword, 
	  InfluxDBClient* client, 
	  IStatus* status)
	{
	  Serial.println("WiFi... ");

	  status->setStatus(Status::WIFI_CONNECTING);

	  // Setup wifi
	  WiFi.mode(WIFI_STA);
	  WiFi.begin(wifiSSID, wifiPassword);

	  while (WiFi.status() != WL_CONNECTED)
	  {
		 Serial.print(".");
		 delay(100);
	  }
	  Serial.println("ok");

	  status->setStatus(Status::WEB_CONNECTING);

	  // Accurate time is necessary for certificate validation and writing in batches
	  // We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
	  // Syncing progress and the time will be printed to Serial.
	  Serial.print("Syncing Time... ");
	  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
	  Serial.println("ok");

	  // Check server connection
	  Serial.print("Influx... ");

	  if (client->validateConnection())
	  {
		 Serial.println("ok");
		 Serial.println(client->getServerUrl());
		 status->setStatus(Status::READY);
	  }
	  else
	  {
		 Serial.println("FAILED");
		 Serial.println(client->getLastErrorMessage());
		 while (1);
	  }
   }
}

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
   Field(std::string name, uint8_t decimalPlaces)
   {
	  _name = name;
	  _decimalPlaces = decimalPlaces;
   }

   virtual ~Field()
   {
   }

   std::string getName()
   {
	  return _name;
   }

   uint8_t getDecimalPlaces()
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
	ValueField(std::string name, uint8_t decimalPlaces) : Field(name, decimalPlaces)
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
// Tracks a time-windowed average for a field using TimedStats over the configured duration.
//
//-------------------------------------------------------------------------------------------------
class TimeAveragedField : public Field
{
private:
	TimedStats* _stats;

public:
	TimeAveragedField(float seconds, std::string name, uint8_t decimalPlaces) : Field(name, decimalPlaces)
	{
		_stats = new TimedStats(1000 * seconds);
	}

	~TimeAveragedField() override
	{
		delete _stats;
	}

	void set(float value) override
	{
		_stats->set(value);
	}

	float get() override
	{
		return _stats->average();
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

	SimplePoint(const char* measurement, std::vector<std::pair<const char*, const char*>> tags) : _point(measurement)
	{
		for (size_t i = 0; i < tags.size(); i++)
		{
			_point.addTag(tags[i].first, tags[i].second);
		}
	}

	~SimplePoint()
	{
		for (size_t i = 0; i < _fields.size(); i++)
		{
			delete _fields[i];
		}
	}

   void addTag(const String& name, String value)
   {
	  _point.addTag(name, value);
   }

   Field* addValueField(std::string name, uint8_t decimalPlaces)
   {
	  Field* field = new ValueField(name, decimalPlaces);
	  _fields.push_back(field);
	  return field;
   }

   Field* addTimeAveragedField(float seconds, std::string name, uint8_t decimalPlaces)
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
	  for (size_t i = 0; i < _fields.size(); i++)
	  {
		 Field* field = _fields[i];
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
