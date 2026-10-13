#pragma once

#include <Preferences.h>
#include <span>
#include <sstream>
#include <string>

#include "DeviceServerClient.h"

///
/// <summary>
/// Shared base for classes that resolve a site (chosen from a fixed list) and location
/// (free text) pair and expose them remotely via the "GetConfig"/"SetConfig"
/// DeviceServer commands, including the interactive shell "SetConfig" prompt flow (no
/// arguments, answered line-by-line like the Serial setup prompt). Subclasses
/// (SiteLocationResolver, MonitorSketch) own their own resolve/prompt-on-boot sequence
/// and persistence timing, but share the command registration, validation, persistence,
/// and interactive prompting logic here. Subclasses may add extra config keys (e.g.
/// MonitorSketch's "bucket") by overriding _extraConfigText() and _trySetExtraConfig().
/// Only one DeviceServerClient::onCommand() handler is supported at a time, so don't use
/// a subclass of this class if the sketch registers its own handler.
/// </summary>
///
class ConfigCommandHandler
{
protected:
   static constexpr auto SITE_KEY = "site";
   static constexpr auto LOCATION_KEY = "location";

   const char* _preferencesNamespace = nullptr;
   std::span<const char* const> _siteOptions;
   String _siteName;
   String _locationName;
   Preferences* _commandPreferences = nullptr;

   /// <summary>Site name chosen during an in-progress interactive SetConfig prompt (see _sendSitePrompt()), pending location entry.</summary>
   String _pendingSiteSelection;

   /// <summary>Accumulated "key=value" lines for values persisted during the in-progress interactive SetConfig prompt, printed together once the flow completes (see _finishInteractiveConfig()).</summary>
   std::string _pendingInteractiveSummary;

   /// <summary>The single instance, used by the static _onConfigCommand() bridge function (DeviceServerClient::onCommand() only accepts a captureless function pointer).</summary>
   inline static ConfigCommandHandler* _configInstance = nullptr;

   ///
   /// <summary>
   /// Returns additional "key=value" line(s) to append to the GetConfig response, or
   /// empty if the subclass has no extra config values. Overridden by subclasses that
   /// add config keys beyond site/location (e.g. MonitorSketch's bucket).
   /// </summary>
   /// <returns>Extra config text (no leading/trailing newline), or empty.</returns>
   ///
   virtual std::string _extraConfigText()
   {
      return std::string();
   }

   ///
   /// <summary>
   /// Attempts to persist a config key beyond site/location (e.g. MonitorSketch's
   /// bucket). Overridden by subclasses that support extra config keys.
   /// </summary>
   /// <param name="key">The config key, already known not to be "site" or "location".</param>
   /// <param name="value">The new value.</param>
   /// <param name="result">Set to the response message when the key is recognized.</param>
   /// <returns>True if the key was recognized (whether or not the value was valid); otherwise false.</returns>
   ///
   virtual bool _trySetExtraConfig(const std::string& key, const std::string& value, std::string& result)
   {
      (void)key;
      (void)value;
      (void)result;
      return false;
   }

   ///
   /// <summary>
   /// Builds the "GetConfig" response text: the current site and location, plus any
   /// subclass-supplied extra config values (see _extraConfigText()).
   /// </summary>
   /// <returns>The formatted config text.</returns>
   ///
   std::string _getConfigText()
   {
      std::string text = std::string("site=") + _siteName.c_str() + "\nlocation=" + _locationName.c_str();
      std::string extra = _extraConfigText();
      if (!extra.empty())
      {
         text += "\n" + extra;
      }
      return text;
   }

   ///
   /// <summary>
   /// Persists a new config value to Preferences via _commandPreferences, validating
   /// site against _siteOptions (location is free text; other keys are delegated to
   /// _trySetExtraConfig()). Does not take effect until the device is restarted, since
   /// site/location are only resolved at boot.
   /// </summary>
   /// <param name="key">Which value to change, e.g. "site" or "location".</param>
   /// <param name="value">The new value.</param>
   /// <param name="includeRestartNote">If true, appends a "Restart required to apply." note to the result.</param>
   /// <returns>A response message describing the result.</returns>
   ///
   std::string _setConfig(const std::string& key, const std::string& value, bool includeRestartNote = true)
   {
      if (value.empty())
      {
         return "Error: value must not be empty";
      }

      if (strcasecmp(key.c_str(), "site") == 0)
      {
         bool valid = false;
         for (const char* option : _siteOptions)
         {
            if (strcasecmp(option, value.c_str()) == 0)
            {
               valid = true;
               break;
            }
         }
         if (!valid)
         {
            return "Error: unknown site \"" + value + "\"";
         }

         _persistString(SITE_KEY, value.c_str());
         return "Saved site=\"" + value + "\"." + (includeRestartNote ? " Restart required to apply." : "");
      }
      else if (strcasecmp(key.c_str(), "location") == 0)
      {
         _persistString(LOCATION_KEY, value.c_str());
         return "Saved location=\"" + value + "\"." + (includeRestartNote ? " Restart required to apply." : "");
      }

      std::string result;
      if (_trySetExtraConfig(key, value, result))
      {
         return result;
      }

      return "Error: unknown config key \"" + key + "\"";
   }

   ///
   /// <summary>
   /// Persists a single string value to Preferences under _preferencesNamespace.
   /// </summary>
   /// <param name="key">Preferences key.</param>
   /// <param name="value">Value to store.</param>
   ///
   void _persistString(const char* key, const char* value)
   {
      _commandPreferences->begin(_preferencesNamespace, false);
      _commandPreferences->putString(key, value);
      _commandPreferences->end();
   }

   ///
   /// <summary>
   /// Handles the "GetConfig"/"SetConfig" commands registered via _enableConfigCommands().
   /// Registered with DeviceServerClient::onCommand() since it only accepts a captureless
   /// function pointer.
   /// </summary>
   /// <param name="command">Full command text received from the DeviceServer.</param>
   ///
   static void _onConfigCommand(const char* command)
   {
      std::istringstream stream(command);
      std::string verb;
      stream >> verb;

      if (strcasecmp(verb.c_str(), "GetConfig") == 0)
      {
         DeviceServerClient::respond(_configInstance->_getConfigText().c_str());
      }
      else if (strcasecmp(verb.c_str(), "SetConfig") == 0)
      {
         std::string key;
         stream >> key;

         if (key.empty())
         {
            if (DeviceServerClient::isShellCommand())
            {
               _configInstance->_sendSitePrompt();
            }
            else
            {
               DeviceServerClient::respond("Error: usage SetConfig <key> <value> (issue SetConfig with no arguments from the shell to be prompted instead)");
            }
            return;
         }

         std::string value;
         std::getline(stream >> std::ws, value);
         DeviceServerClient::respond(_configInstance->_setConfig(key, value).c_str());
      }
      else
      {
         DeviceServerClient::respond((std::string("Unhandled command: ") + command).c_str());
      }
   }

   ///
   /// <summary>
   /// Persists a config value via _setConfig() during the interactive SetConfig flow,
   /// accumulating a plain "key=value" line into _pendingInteractiveSummary on success
   /// (all such lines are printed together once the flow completes; see
   /// _finishInteractiveConfig()), or printing an "Error: ..." line immediately on
   /// failure.
   /// </summary>
   /// <param name="key">Which value to change, e.g. "site", "location", or "bucket".</param>
   /// <param name="value">The new value.</param>
   ///
   void _applyInteractiveConfig(const std::string& key, const std::string& value)
   {
      std::string result = _setConfig(key, value, false);
      if (result.rfind("Error:", 0) == 0)
      {
         DeviceServerClient::shell().println(result.c_str());
         return;
      }

      if (!_pendingInteractiveSummary.empty())
      {
         _pendingInteractiveSummary += "\n";
      }
      _pendingInteractiveSummary += key + "=" + value;
   }

   ///
   /// <summary>
   /// Completes the interactive SetConfig flow: prints the accumulated "key=value"
   /// summary (see _applyInteractiveConfig()) and responds that a restart is required.
   /// </summary>
   ///
   void _finishInteractiveConfig()
   {
      DeviceServerClient::shell().println(_pendingInteractiveSummary.c_str());
      _pendingInteractiveSummary.clear();
      DeviceServerClient::respond("Restart required to apply.");
   }

   ///
   /// <summary>
   /// Writes the "Select a site" prompt to the shell channel and registers
   /// _onInteractiveSiteAnswer() to handle the next shell input line as the answer. Used
   /// by the no-argument "SetConfig" shell command to mirror the Serial setup prompt.
   /// </summary>
   ///
   void _sendSitePrompt()
   {
      DeviceServerClient::shell().println("Select a site:");
      for (size_t i = 0; i < _siteOptions.size(); i++)
      {
         DeviceServerClient::shell().print("  ");
         DeviceServerClient::shell().print(i + 1);
         DeviceServerClient::shell().print(": ");
         DeviceServerClient::shell().println(_siteOptions[i]);
      }

      String label = "Enter selection (1-" + String(_siteOptions.size()) + ")";
      if (_siteName.length() > 0)
      {
         label += " [" + _siteName + "]";
      }
      label += ": ";
      DeviceServerClient::shell().print(label);
      DeviceServerClient::shell().flushOutput();

      DeviceServerClient::promptShellLine(_onInteractiveSiteAnswer);
   }

   ///
   /// <summary>
   /// Handles the answer to the interactive SetConfig site prompt sent via
   /// promptShellLine(): a blank answer keeps the current site (if any), otherwise
   /// validates the selection, reprompting if invalid; once a site is settled on,
   /// prompts for the location.
   /// </summary>
   /// <param name="line">The shell input line entered in response to the site prompt.</param>
   ///
   static void _onInteractiveSiteAnswer(const char* line)
   {
      String input = String(line);
      input.trim();

      if (input.length() == 0 && _configInstance->_siteName.length() > 0)
      {
         _configInstance->_pendingSiteSelection = _configInstance->_siteName;
         _configInstance->_sendLocationPrompt();
         return;
      }

      long selection = input.toInt();

      if (selection < 1 || selection > (long)_configInstance->_siteOptions.size())
      {
         DeviceServerClient::shell().println("Invalid selection.");
         _configInstance->_sendSitePrompt();
         return;
      }

      _configInstance->_pendingSiteSelection = _configInstance->_siteOptions[selection - 1];
      _configInstance->_sendLocationPrompt();
   }

   ///
   /// <summary>
   /// Writes the "Enter location" prompt to the shell channel, showing the current
   /// location (if any) as the default, and registers _onInteractiveLocationAnswer() to
   /// handle the next shell input line as the answer.
   /// </summary>
   ///
   void _sendLocationPrompt()
   {
      String label = "Enter location";
      if (_locationName.length() > 0)
      {
         label += " [" + _locationName + "]";
      }
      label += ": ";
      DeviceServerClient::shell().print(label);
      DeviceServerClient::shell().flushOutput();
      DeviceServerClient::promptShellLine(_onInteractiveLocationAnswer);
   }

   ///
   /// <summary>
   /// Handles the answer to the interactive SetConfig location prompt sent via
   /// promptShellLine(): reprompts if empty, otherwise persists the selected site and
   /// entered location and responds with the result.
   /// </summary>
   /// <param name="line">The shell input line entered in response to the location prompt.</param>
   ///
   static void _onInteractiveLocationAnswer(const char* line)
   {
      String location = String(line);
      location.trim();

      if (location.length() == 0)
      {
         if (_configInstance->_locationName.length() > 0)
         {
            location = _configInstance->_locationName;
         }
         else
         {
            _configInstance->_sendLocationPrompt();
            return;
         }
      }

      _configInstance->_applyInteractiveConfig("site", _configInstance->_pendingSiteSelection.c_str());
      _configInstance->_applyInteractiveConfig("location", location.c_str());

      if (_configInstance->_hasExtraInteractivePrompt())
      {
         _configInstance->_sendExtraPrompt();
      }
      else
      {
         _configInstance->_finishInteractiveConfig();
      }
   }

   ///
   /// <summary>
   /// Returns true if a subclass has an extra interactive prompt (e.g. MonitorSketch's
   /// bucket) to show after the location step completes, during the interactive shell
   /// "SetConfig" flow. Overridden by subclasses that add extra config keys they want
   /// included in that flow.
   /// </summary>
   /// <returns>True if _sendExtraPrompt() should be called next; otherwise false.</returns>
   ///
   virtual bool _hasExtraInteractivePrompt()
   {
      return false;
   }

   ///
   /// <summary>
   /// Sends the subclass's extra interactive prompt (e.g. MonitorSketch's bucket
   /// selection) and registers the callback for its answer. Only called when
   /// _hasExtraInteractivePrompt() returns true. The override is responsible for calling
   /// DeviceServerClient::respond() once done (typically with "Restart required to
   /// apply.").
   /// </summary>
   ///
   virtual void _sendExtraPrompt()
   {
   }

   ///
   /// <summary>
   /// Registers the "GetConfig"/"SetConfig" commands with DeviceServerClient, letting the
   /// DeviceServer list and remotely change this device's persisted config. Changes made
   /// via "SetConfig" are saved to Preferences but require a "Restart" command to take
   /// effect, since site/location are only resolved at boot.
   /// </summary>
   /// <param name="commands">The subclass's CommandInfo array, listed by "Help" after the built-ins.</param>
   /// <param name="count">Number of entries in commands.</param>
   ///
   void _enableConfigCommands(const DeviceServerClient::CommandInfo* commands, size_t count)
   {
      _configInstance = this;
      DeviceServerClient::onCommand(_onConfigCommand, commands, count);
   }

public:
   virtual ~ConfigCommandHandler() = default;

   ///
   /// <summary>Returns the resolved site name (empty until the subclass resolves it).</summary>
   ///
   const String& site() const
   {
      return _siteName;
   }

   ///
   /// <summary>Returns the resolved location name (empty until the subclass resolves it).</summary>
   ///
   const String& location() const
   {
      return _locationName;
   }
};
