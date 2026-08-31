#pragma once

///
/// <summary>
/// Interface for a destination that startup/init text can be logged to (e.g. Serial). Add
/// instances via ArduinoBase::addLogger() (e.g. addLogger(new SerialLogger())); multiple
/// loggers can be active at once, and additional logger types (e.g. a file logger, a
/// network logger) can be added later without changing any calling code.
/// </summary>
///
class ILogger
{
public:
   virtual ~ILogger() = default;

   ///
   /// <summary>Writes text without a trailing newline.</summary>
   /// <param name="str">The text to write.</param>
   ///
   virtual void write(const char* str) = 0;

   ///
   /// <summary>Writes text followed by a newline.</summary>
   /// <param name="str">The text to write.</param>
   ///
   virtual void writeln(const char* str) = 0;
};
