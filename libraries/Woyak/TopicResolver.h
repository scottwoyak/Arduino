#pragma once

// Requires the sketch to have already included ArduinoBoard.h (so the Arduino class is
// defined).

#include <string>

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
   static constexpr const char* PENDING_REMOVAL_KEY = "topicRemove";
   static constexpr SerialTable::Column TOPIC_COLUMNS[] = {
      { "Topic", 24 },
   };

   const char* _preferencesNamespace;
   const TelemetryConfig* _config;
   PreferencesResolver _resolver;
   std::string _topic;
   bool _resolved = false;

public:
   ///
   /// <summary>
   /// Creates a TopicResolver for the given telemetry configuration.
   /// </summary>
   /// <param name="preferencesNamespace">Preferences (NVS) namespace used to persist the selected topic. Only needed if config->prompts is non-empty.</param>
   /// <param name="config">Telemetry configuration, owned by the caller and expected to outlive this object.</param>
   ///
   TopicResolver(const char* preferencesNamespace, const TelemetryConfig* config)
      : _preferencesNamespace(preferencesNamespace),
        _config(config),
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
         if (_config->topic != nullptr)
         {
            arduino->preferences.begin(_preferencesNamespace, false);
            if (!arduino->preferences.isKey(TOPIC_KEYS[0]))
            {
               arduino->preferences.putString(TOPIC_KEYS[0], _config->topic);
            }
            arduino->preferences.end();
         }

         std::span<const String> resolved = _resolver.resolve(arduino, status, "Select a telemetry topic (* = default):", TOPIC_COLUMNS, _config->prompts.data(), _config->prompts.size(), forcePrompt);
         _topic = resolved[0].c_str();
      }
      else
      {
         _topic = _config->topic;
      }

      _resolved = true;
      return _topic.c_str();
   }

   ///
   /// <summary>
   /// Returns the resolved telemetry topic (nullptr until resolve() is called).
   /// </summary>
   /// <returns>The resolved topic.</returns>
   ///
   const char* topic() const
   {
      return _resolved ? _topic.c_str() : nullptr;
   }

   ///
   /// <summary>
   /// Persists a new telemetry topic to Preferences, validated against config.prompts.
   /// By default the selection takes effect after restart; publishers may apply it immediately.
   /// Only valid when hasPrompts() is true.
   /// </summary>
   /// <param name="newTopic">The new topic to select, matched case-sensitively against config.prompts.</param>
   /// <param name="applyImmediately">Update the resolved topic immediately if true.</param>
   /// <param name="retireOnRestart">Persist the active topic for removal after restart.</param>
   /// <returns>A response message describing the result.</returns>
   ///
   std::string trySetTopic(const std::string& newTopic, bool applyImmediately = false, bool retireOnRestart = false)
   {
      if (!hasPrompts())
      {
         return "Error: this device doesn't support changing the telemetry topic";
      }

      if (newTopic.empty())
      {
         return "Error: value must not be empty";
      }

      bool valid = false;
      for (const char* option : _config->prompts)
      {
         if (strcmp(option, newTopic.c_str()) == 0)
         {
            valid = true;
            break;
         }
      }
      if (!valid)
      {
         return "Error: unknown topic \"" + newTopic + "\"";
      }

      Preferences preferences;
      if (!preferences.begin(_preferencesNamespace, false))
      {
         return "Error: could not open topic preferences";
      }
      if (retireOnRestart && _resolved && _topic != newTopic && preferences.putString(PENDING_REMOVAL_KEY, _topic.c_str()) == 0)
      {
         preferences.end();
         return "Error: could not save topic removal";
      }
      size_t written = preferences.putString(TOPIC_KEYS[0], newTopic.c_str());
      if (written != 0 && retireOnRestart && _resolved && _topic == newTopic && preferences.isKey(PENDING_REMOVAL_KEY))
      {
         if (!preferences.remove(PENDING_REMOVAL_KEY))
         {
            preferences.end();
            return "Error: could not clear pending topic removal";
         }
      }
      preferences.end();
      if (written == 0)
      {
         return "Error: could not save topic";
      }

      if (applyImmediately)
      {
         _topic = newTopic;
         _resolved = true;
         return "Saved topic=\"" + newTopic + "\". Telemetry change queued.";
      }
      return "Saved topic=\"" + newTopic + "\". Restart required to apply.";
   }

   ///
   /// <summary>
   /// Reads a topic whose removal was deferred until restart.
   /// </summary>
   /// <returns>The pending topic, or an empty string if none is recorded.</returns>
   ///
   std::string pendingRemoval() const
   {
      Preferences preferences;
      if (!preferences.begin(_preferencesNamespace, true))
      {
         Logger.log("Could not read pending telemetry topic removal", LogSeverity::ERROR, "Telemetry");
         return "";
      }
      std::string topic = preferences.getString(PENDING_REMOVAL_KEY, "").c_str();
      preferences.end();
      return topic;
   }

   ///
   /// <summary>
   /// Clears a recorded removal only if it still refers to the given topic.
   /// </summary>
   /// <param name="topic">Topic whose removal has completed or is no longer needed.</param>
   ///
   void clearPendingRemoval(const std::string& topic)
   {
      Preferences preferences;
      if (!preferences.begin(_preferencesNamespace, false))
      {
         Logger.log("Could not open pending telemetry topic removal", LogSeverity::ERROR, "Telemetry");
         return;
      }
      if (preferences.getString(PENDING_REMOVAL_KEY, "") == topic.c_str() && !preferences.remove(PENDING_REMOVAL_KEY))
      {
         Logger.log("Could not clear pending telemetry topic removal: " + topic, LogSeverity::ERROR, "Telemetry");
      }
      preferences.end();
   }
};
