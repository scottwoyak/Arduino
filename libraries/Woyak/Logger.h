#pragma once

#include <string>
#include <vector>

#include "ILogger.h"
#include "SerialLogger.h"

///
/// <summary>
/// Global logging destination registry for startup/init text. A SerialLogger is active
/// by default, so startup output is always echoed to Serial with no setup required;
/// additional destinations (e.g. a future file or network logger) can be added via
/// Logger::addLogger(). Call Logger::clearLoggers() once initialization is complete (see
/// ArduinoBase::beginInit()/clearLoggers()) so routine loop() output isn't logged.
/// Unlike ArduinoBase's print()/println(), this is reachable from anywhere in a sketch,
/// not just from an ArduinoBase-derived instance.
/// </summary>
///
class Logger
{
private:
   inline static std::vector<ILogger*> _loggers{ new SerialLogger() };

public:
   ///
   /// <summary>
   /// Adds a logger to receive write()/writeln() output (e.g. Logger::addLogger(new
   /// SerialLogger())). Ownership of the passed-in instance transfers to Logger (it is
   /// never deleted, matching the lifetime of other statically-allocated helpers in
   /// these sketches). Multiple loggers can be active at once.
   /// </summary>
   /// <param name="logger">The logger to add.</param>
   ///
   static void addLogger(ILogger* logger)
   {
      _loggers.push_back(logger);
   }

   ///
   /// <summary>
   /// Removes all active loggers. Call at the end of setup(), once initialization is
   /// complete, so routine loop() output (e.g. live sensor values, telemetry chatter)
   /// doesn't keep getting logged.
   /// </summary>
   ///
   static void clearLoggers()
   {
      _loggers.clear();
   }

   ///
   /// <summary>Returns true if at least one logger is currently active.</summary>
   /// <returns>True if at least one logger is currently active.</returns>
   ///
   static bool hasLoggers()
   {
      return !_loggers.empty();
   }

   ///
   /// <summary>Writes text without a trailing newline to all active loggers.</summary>
   /// <param name="str">The text to write.</param>
   ///
   static void write(const char* str)
   {
      for (ILogger* logger : _loggers)
      {
         logger->write(str);
      }
   }

   ///
   /// <summary>Writes text followed by a newline to all active loggers.</summary>
   /// <param name="str">The text to write.</param>
   ///
   static void writeln(const char* str)
   {
      for (ILogger* logger : _loggers)
      {
         logger->writeln(str);
      }
   }

   ///
   /// <summary>Writes text without a trailing newline to all active loggers.</summary>
   /// <param name="str">The text to write.</param>
   ///
   static void write(const String& str)
   {
      write(str.c_str());
   }

   ///
   /// <summary>Writes text followed by a newline to all active loggers.</summary>
   /// <param name="str">The text to write.</param>
   ///
   static void writeln(const String& str)
   {
      writeln(str.c_str());
   }

   ///
   /// <summary>Writes text without a trailing newline to all active loggers.</summary>
   /// <param name="str">The text to write.</param>
   ///
   static void write(const std::string& str)
   {
      write(str.c_str());
   }

   ///
   /// <summary>Writes text followed by a newline to all active loggers.</summary>
   /// <param name="str">The text to write.</param>
   ///
   static void writeln(const std::string& str)
   {
      writeln(str.c_str());
   }
};
