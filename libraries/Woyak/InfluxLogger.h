#pragma once

#include <InfluxDbClient.h>
#include <string>
#include <utility>
#include <vector>

#include "ILogger.h"
#include "Influx.h"

///
/// <summary>
/// ILogger implementation that posts log lines to InfluxDB as points, so setup()/init text
/// (and any other Logger::write()/writeln() calls) is retained in Influx alongside sensor
/// telemetry instead of only appearing on Serial. Create via Logger::addLogger(new
/// InfluxLogger("Log", { { "site", site.c_str() }, { "location", location.c_str() }, { "sensor",
/// INFLUX_SENSOR } })) as soon as the tags are known (an Influx instance is not required yet);
/// lines written before an Influx instance is attached are queued in memory. Once an Influx
/// instance is available (i.e. after Influx::begin() succeeds), call attach(influx) to start
/// posting new lines directly and flush the queued ones (queued lines are posted with the
/// current time, since no synced clock is available before then). Text passed to write() is
/// buffered and posted as a single "message" field once writeln() completes the line, matching
/// ILogger's write()/writeln() split.
/// </summary>
///
class InfluxLogger : public ILogger
{
private:
   /// <summary>InfluxDB client used to post log points; nullptr until attach() is called.</summary>
   InfluxDBClient* _client = nullptr;

   /// <summary>Influx measurement name used for log points.</summary>
   std::string _measurement;

   /// <summary>Tags attached to each log point (e.g. site, location, sensor).</summary>
   std::vector<std::pair<std::string, std::string>> _tags;

   /// <summary>Accumulates write() calls until writeln() completes a line.</summary>
   std::string _buffer;

   /// <summary>Lines written before attach() is called, flushed (with the then-current time) once attached.</summary>
   std::vector<std::string> _pending;

   ///
   /// <summary>
   /// Posts the given message as a single-field Influx point using the current time.
   /// </summary>
   /// <param name="message">Message text to post</param>
   ///
   void _post(const std::string& message)
   {
      Point point(_measurement.c_str());
      for (const auto& tag : _tags)
      {
         point.addTag(tag.first.c_str(), tag.second.c_str());
      }
      point.addField("message", message.c_str());

      if (!_client->writePoint(point))
      {
         Serial.print("InfluxDB log write failed: ");
         Serial.println(_client->getLastErrorMessage());
      }
   }

public:
   ///
   /// <summary>
   /// Creates an InfluxLogger that queues lines in memory until attach() is called.
   /// </summary>
   /// <param name="measurement">Influx measurement name for log points</param>
   /// <param name="tags">Key/value pairs attached as tags to each log point (e.g. site, location, sensor)</param>
   ///
   InfluxLogger(const char* measurement, const std::vector<std::pair<const char*, const char*>>& tags = {})
   {
      _measurement = measurement;
      for (const auto& tag : tags)
      {
         _tags.emplace_back(tag.first, tag.second);
      }
   }

   ~InfluxLogger() override = default;

   ///
   /// <summary>
   /// Attaches this logger to an Influx instance's client, then immediately posts (with the
   /// current time) any lines that were queued before this call.
   /// </summary>
   /// <param name="influx">Influx instance whose client is used to post log messages</param>
   ///
   void attach(Influx* influx)
   {
      _client = influx->client();

      for (const std::string& message : _pending)
      {
         _post(message);
      }
      _pending.clear();
   }

   ///
   /// <summary>Buffers text without a trailing newline; posted once writeln() completes the line.</summary>
   /// <param name="str">The text to write.</param>
   ///
   void write(const char* str) override
   {
      _buffer += str;
   }

   ///
   /// <summary>Buffers text, then posts the accumulated line as an Influx point.</summary>
   /// <param name="str">The text to write.</param>
   ///
   void writeln(const char* str) override
   {
      _buffer += str;

      if (_client != nullptr)
      {
         _post(_buffer);
      }
      else
      {
         _pending.push_back(_buffer);
      }

      _buffer.clear();
   }
};
