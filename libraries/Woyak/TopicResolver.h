#pragma once

// Requires the sketch to have already included ArduinoBoard.h (so the Arduino class is
// defined).

#include "Logger.h"
#include "PreferencesResolver.h"
#include "Status.h"
#include "TelemetryConfig.h"
#include "Util.h"

///
/// <summary>
/// Topic resolution shared by every sketch that publishes or subscribes to a telemetry
/// topic (PublisherSketch, ViewerSketch): resolves the topic (either the fixed
/// TelemetryConfig::topic, or one selected from TelemetryConfig::prompts and persisted
/// in Preferences). Sketch base classes that need
/// telemetry hold one of these as a member.
/// </summary>
///
class TopicResolver
{
private:
   static constexpr const char* TOPIC_KEYS[] = { "topic" };
   static constexpr SerialTable::Column TOPIC_COLUMNS[] = {
      { "Topic", 24 },
   };

   const TelemetryConfig* _config;
   PreferencesResolver _resolver;
   const char* _topic = nullptr;

public:
   ///
   /// <summary>
   /// Creates a TopicResolver for the given telemetry configuration.
   /// </summary>
   /// <param name="preferencesNamespace">Preferences (NVS) namespace used to persist the selected topic. Only needed if config->prompts is non-empty.</param>
   /// <param name="config">Telemetry configuration, owned by the caller and expected to outlive this object.</param>
   ///
   TopicResolver(const char* preferencesNamespace, const TelemetryConfig* config)
      : _config(config),
        _resolver(preferencesNamespace, TOPIC_KEYS)
   {
      ASSERT(config != nullptr);
   }

   ///
   /// <summary>
   /// Returns whether telemetry is configured at all (either a fixed topic or a
   /// selectable topic table).
   /// </summary>
   /// <returns>True if a topic can be resolved.</returns>
   ///
   bool isConfigured() const
   {
      return hasPrompts() || _config->topic != nullptr;
   }

   ///
   /// <summary>
   /// Returns whether the topic is selected from a prompt table (and so may need the
   /// user to be prompted).
   /// </summary>
   /// <returns>True if config->prompts is non-empty.</returns>
   ///
   bool hasPrompts() const
   {
      return !_config->prompts.empty();
   }

   ///
   /// <summary>
   /// Resolves the telemetry topic: the fixed config->topic if there's no prompt table,
   /// otherwise the saved selection (prompting over Serial if none is saved yet or
   /// forcePrompt is true). The caller is responsible for reporting the result.
   /// </summary>
   /// <param name="arduino">The board wrapper, used for Preferences.</param>
   /// <param name="status">Status indicator, set to FAILED if a required prompt can't be shown.</param>
   /// <param name="forcePrompt">If true, always prompts even if a saved topic exists.</param>
   /// <returns>The resolved topic, backed by this object's storage.</returns>
   ///
   const char* resolve(Arduino* arduino, IStatus* status, bool forcePrompt)
   {
      ASSERT(isConfigured());

      if (hasPrompts())
      {
         std::span<const String> resolved = _resolver.resolve(arduino->preferences, status, "Select a telemetry topic (* = default):", TOPIC_COLUMNS, _config->prompts.data(), _config->prompts.size(), forcePrompt);
         _topic = resolved[0].c_str();
      }
      else
      {
         _topic = _config->topic;
      }

      return _topic;
   }

   ///
   /// <summary>
   /// Returns the resolved telemetry topic (nullptr until resolve() is called).
   /// </summary>
   /// <returns>The resolved topic.</returns>
   ///
   const char* topic() const
   {
      return _topic;
   }
};
