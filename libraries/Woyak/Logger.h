#pragma once

#include <string>
#include <vector>

#include "DeviceHubClient.h"

///
/// <summary>
/// Thin delegating wrapper around DeviceHubClient (see DeviceHubClient.h) for the
/// logging-specific portion of its API (log(), logPartial(), setTag()/setTags()).
/// Connection lifecycle and command/status handling (begin(), loop(), isConnected(),
/// onCommand(), onStatus(), respond(), etc.) are DeviceHubClient concerns, not logging,
/// and are called directly via DeviceHubClient:: instead. All members are static since
/// a sketch has a single Device Hub connection; use the global Logger instance below
/// (mirroring Serial), e.g. Logger.log(...).
/// </summary>
///
class LoggerClass
{
public:
   ///
   /// <summary>
   /// Sets the single default tag/component sent with subsequent log()/logPartial() calls
   /// that don't specify their own tags (see DeviceHubClient::setTag()).
   /// </summary>
   /// <param name="tag">Tag text to use as the default from now on.</param>
   ///
   static void setTag(const char* tag)
   {
      DeviceHubClient::setTag(tag);
   }

   ///
   /// <summary>
   /// Sets the default tags/components sent with subsequent log()/logPartial() calls
   /// that don't specify their own tags (see DeviceHubClient::setTags()).
   /// </summary>
   /// <param name="tags">Tags to use as the default from now on.</param>
   ///
   static void setTags(const std::vector<std::string>& tags)
   {
      DeviceHubClient::setTags(tags);
   }

   ///
   /// <summary>
   /// Sends a log message to the Device Hub (see DeviceHubClient::log()). Always echoes
   /// to Serial.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message; WARN/ERROR are prefixed with "WARN: "/"ERROR: ".</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void log(const char* message, LogSeverity severity = LogSeverity::INFO, const std::vector<std::string>& tags = {})
   {
      DeviceHubClient::log(message, severity, tags);
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity, const std::vector<std::string>&) for callers passing a single tag.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message; WARN/ERROR are prefixed with "WARN: "/"ERROR: ".</param>
   /// <param name="tag">Tag/component the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void log(const char* message, LogSeverity severity, const char* tag)
   {
      DeviceHubClient::log(message, severity, tag);
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity, const std::vector<std::string>&) for callers holding a std::string.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message; WARN/ERROR are prefixed with "WARN: "/"ERROR: ".</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void log(const std::string& message, LogSeverity severity = LogSeverity::INFO, const std::vector<std::string>& tags = {})
   {
      DeviceHubClient::log(message, severity, tags);
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity, const char*) for callers holding a std::string and a single tag.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message; WARN/ERROR are prefixed with "WARN: "/"ERROR: ".</param>
   /// <param name="tag">Tag/component the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void log(const std::string& message, LogSeverity severity, const char* tag)
   {
      DeviceHubClient::log(message, severity, tag);
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity, const std::vector<std::string>&) for callers holding an Arduino String.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message; WARN/ERROR are prefixed with "WARN: "/"ERROR: ".</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void log(const String& message, LogSeverity severity = LogSeverity::INFO, const std::vector<std::string>& tags = {})
   {
      DeviceHubClient::log(message, severity, tags);
   }

   ///
   /// <summary>
   /// Overload of log(const char*, LogSeverity, const char*) for callers holding an Arduino String and a single tag.
   /// </summary>
   /// <param name="message">Message text to log.</param>
   /// <param name="severity">Severity of the message; WARN/ERROR are prefixed with "WARN: "/"ERROR: ".</param>
   /// <param name="tag">Tag/component the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void log(const String& message, LogSeverity severity, const char* tag)
   {
      DeviceHubClient::log(message, severity, tag);
   }

   ///
   /// <summary>
   /// Sends a message fragment to the Device Hub without completing the log entry (see
   /// DeviceHubClient::logPartial()).
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void logPartial(const char* message, const std::vector<std::string>& tags = {})
   {
      DeviceHubClient::logPartial(message, tags);
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*, const std::vector<std::string>&) for callers passing a single tag.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   /// <param name="tag">Tag/component the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void logPartial(const char* message, const char* tag)
   {
      DeviceHubClient::logPartial(message, tag);
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*, const std::vector<std::string>&) for callers holding a std::string.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void logPartial(const std::string& message, const std::vector<std::string>& tags = {})
   {
      DeviceHubClient::logPartial(message, tags);
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*, const char*) for callers holding a std::string and a single tag.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   /// <param name="tag">Tag/component the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void logPartial(const std::string& message, const char* tag)
   {
      DeviceHubClient::logPartial(message, tag);
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*, const std::vector<std::string>&) for callers holding an Arduino String.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   /// <param name="tags">Tags/components the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void logPartial(const String& message, const std::vector<std::string>& tags = {})
   {
      DeviceHubClient::logPartial(message, tags);
   }

   ///
   /// <summary>
   /// Overload of logPartial(const char*, const char*) for callers holding an Arduino String and a single tag.
   /// </summary>
   /// <param name="message">Message fragment text to log.</param>
   /// <param name="tag">Tag/component the message is associated with; defaults to the current tags set via setTag()/setTags().</param>
   ///
   static void logPartial(const String& message, const char* tag)
   {
      DeviceHubClient::logPartial(message, tag);
   }

   ///
   /// <summary>
   /// Logs the standard "initialization complete" message shared by every sketch (see
   /// SketchBase::begin() and ViewerSketch::begin()), so the wording can't drift between
   /// sketches. Call once, at the very end of the sketch's boot/init sequence.
   /// </summary>
   ///
   static void logInitializationComplete()
   {
      DeviceHubClient::logInitializationComplete();
   }
};

///
/// <summary>
/// Global Logger instance shared by the whole sketch (one physical board, one Device Hub
/// connection), mirroring Arduino's Serial global. Board-level helpers (see
/// ArduinoBase::printlnInitStatus(), initWifi(), initSensor(), initClient()) log through
/// this automatically, so callers only need to invoke those helpers once to update both
/// the display/Serial and the Device Hub.
/// </summary>
///
inline LoggerClass Logger;
