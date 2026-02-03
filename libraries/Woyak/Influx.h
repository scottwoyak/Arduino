#pragma once

#include <WiFiMulti.h>
WiFiMulti wifiMulti;
constexpr auto DEVICE = "ESP32";

#include <Feather.h>
#include <TimedAverager.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

// Time zone info
constexpr auto TZ_INFO = "UTC-5";

//-------------------------------------------------------------------------------------------------
//
// General Influx functions
//
//-------------------------------------------------------------------------------------------------
namespace Influx
{
   void begin(Feather* feather, const char* wifiSSID, const char* wifiPassword, InfluxDBClient* client)
   {
      feather->print("WiFi... ", Color::LABEL);

      // Setup wifi
      WiFi.mode(WIFI_STA);
      wifiMulti.addAP(wifiSSID, wifiPassword);

      while (wifiMulti.run() != WL_CONNECTED)
      {
         Serial.print(".");
         delay(100);
      }
      feather->printlnR("ok", Color::VALUE);

      // Accurate time is necessary for certificate validation and writing in batches
      // We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
      // Syncing progress and the time will be printed to Serial.
      bool oldEcho = feather->echoToSerial;
      feather->echoToSerial = false; // timeSync prints to Serial
      feather->print("Syncing Time... ", Color::LABEL);
      timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
      feather->printlnR("ok", Color::VALUE);
      feather->echoToSerial = oldEcho;

      // Check server connection
      feather->print("Influx... ", Color::LABEL);

      if (client->validateConnection())
      {
         feather->printlnR("ok", Color::VALUE);
         Serial.println(client->getServerUrl());
      }
      else
      {
         feather->printlnR("FAILED", Color::RED);
         feather->display.setTextSize(1);
         feather->println(client->getLastErrorMessage(), Color::RED);
         while (1);
      }
   }

   void startInit(Feather* feather)
   {
      feather->echoToSerial = true;
      feather->clearDisplay();
      feather->println("Initializing", Color::HEADING);
      feather->moveCursorY(feather->charH() / 2);
   }

   void endInit(Feather* feather)
   {
      feather->echoToSerial = false;
      feather->clearDisplay();
   }
}

//-------------------------------------------------------------------------------------------------
//
// Field abstract base class
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
// ValueField class
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

   virtual ~ValueField()
   {
   }

   void set(float value)
   {
      _value = value;
   }

   float get()
   {
      return _value;
   }
};

//-------------------------------------------------------------------------------------------------
//
// TimedAveragedField class
//
//-------------------------------------------------------------------------------------------------
class TimeAveragedField : public Field
{
private:
   TimedAverager* _averager;

public:
   TimeAveragedField(float seconds, std::string name, uint8_t decimalPlaces) : Field(name, decimalPlaces)
   {
      _averager = new TimedAverager(1000 * seconds);
   }

   ~TimeAveragedField()
   {
      delete _averager;
   }

   void set(float value)
   {
      _averager->set(value);
   }

   float get()
   {
      return _averager->get();
   }
};

//-------------------------------------------------------------------------------------------------
//
// TimedPoint class
//
//-------------------------------------------------------------------------------------------------
class TimedPoint
{
private:
   Point _point;
   Stopwatch _sw;
   float _seconds;
   std::vector<Field*> _fields;

public:
   TimedPoint(float seconds, const char* measurement) : _point(measurement), _sw(false)
   {
      _seconds = seconds;
   }

   TimedPoint(float seconds, const char* measurement, std::vector<std::pair<const char*, const char*>> tags) : _point(measurement)
   {
      _seconds = seconds;

      for (int i = 0; i < tags.size(); i++)
      {
         _point.addTag(tags[i].first, tags[i].second);
      }
   }

   ~TimedPoint()
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

   Field* addTimeAveragedField(std::string name, uint8_t decimalPlaces)
   {
      Field* field = new TimeAveragedField(_seconds, name, decimalPlaces);
      _fields.push_back(field);
      return field;
   }

   bool ready()
   {
      // if this is the first time this function has been called, start the timer
      if (_sw.isRunning() == false)
      {
         _sw.start();
      }

      return _sw.elapsedSecs() > _seconds;
   }

   bool post(InfluxDBClient* client)
   {
      // restart the timer
      _sw.reset();

      // clear out the old values
      _point.clearFields();

      // populate new values
      for (size_t i = 0; i < _fields.size(); i++)
      {
         Field* field = _fields[i];
         _point.addField(field->getName().c_str(), field->get(), field->getDecimalPlaces());
      }

      //Serial.println(_point.toLineProtocol());

      // send to Influx
      return client->writePoint(_point);
   }
};