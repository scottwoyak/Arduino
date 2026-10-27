//
// Air Subscriber Fan
//
// Subscribes to the AirQuality telemetry topic published by Air_Monitor_Display
// (AirQuality/site/location, reporting the worst 0-5 air quality tier across all sensors)
// and drives a fan control pin (GPIO 5, active HIGH) whenever the tier drops outside of the
// green (Good, tier 0) air quality tier.
//
// Behavior:
// - Prompts for the target location on first boot (or when buttonA is held at startup)
//   and saves the selection in Preferences (site is always "Bragg").
// - Turns the fan pin HIGH immediately when a received AirQuality tier is >= 1 (outside
//   the green/Good tier) and sets (or extends) the fan run timer to 10 minutes. Each
//   subsequent bad-air sample keeps resetting the timer back to at least 10 minutes, so
//   the fan stays on while air quality is bad and for 10 minutes after it returns to 0.
// - Exposes DeviceServer commands (GetState, On [minutes], On1, On10, On20, On30, Off) to
//   inspect or manually run/stop the fan for a timed duration.
//

#include <Arduino.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "ArduinoBoard.h"

#include "AirQualityTiers.h"
#include "LibraryVersion.h"
#include "Logger.h"
#include "SerialX.h"
#include "Stopwatch.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "ViewerSketch.h"

#include "WiFiSettings.h"

const auto VERSION = MakeVersion("1.1");
constexpr auto SKETCH_NAME = "Air_Subscriber_Fan";
constexpr auto PREFERENCES_NAMESPACE = "AirSubFan";
constexpr auto AIR_QUALITY_TOPIC_PREFIX = "AirQuality";

constexpr uint8_t FAN_PIN = 5;
constexpr uint8_t BAD_AIR_RUN_M = 10;
constexpr float DEFAULT_TRIGGER_LEVEL = 1.0f;
constexpr auto TRIGGER_LEVEL_PREF_KEY = "triggerLevel";

// Site for the air monitor to subscribe to; location is entered as free text.
constexpr std::array<const char*, 1> SITE_OPTIONS = { "Bragg" };

SketchConfig SKETCH_CONFIG = {
	.sketchName = SKETCH_NAME,
	.version = VERSION,
	.preferencesNamespace = PREFERENCES_NAMESPACE,
	.cpuFrequencyMhz = 80,
	.enableOTA = true,
};

TelemetryConfig TELEMETRY_CONFIG = {
	.topic = "unresolved",
};

ViewerSketch sketch(SKETCH_CONFIG, TELEMETRY_CONFIG, SITE_OPTIONS);
TimerSecs fanTimer(0.0f);
Stopwatch fanOnStopwatch(false, StopwatchPrecision::Millis);

std::string airQualityTopic;
int8_t currentTier = -1;
float currentScore = NAN;
float triggerLevel = DEFAULT_TRIGGER_LEVEL;
bool fanOn = false;

///
/// <summary>
/// Commands registered with DeviceServerClient, listed by "Help" after the built-ins.
/// </summary>
///
constexpr uint8_t NUM_FAN_COMMANDS = 9;
constexpr std::array<DeviceServerClient::CommandInfo, NUM_FAN_COMMANDS> FAN_COMMANDS = {{
	{ "GetState", "Reports the current fan state, remaining run timer, and air quality tier", { "gs" } },
	{ "GetTriggerLevel", "Reports the air quality score threshold that triggers the fan", { "gtl" } },
	{
		"SetTriggerLevel",
		"Sets and persists the air quality score threshold (0.0-5.0) that triggers the fan",
		{ "stl" },
		{
			{ "level", "Trigger air quality score (0.0-5.0)", true },
		},
	},
	{
		"On",
		"Turns the fan on for the given number of minutes (default 1)",
		{},
		{
			{ "minutes", "Duration in minutes", false, "1" },
		},
	},
	{ "On1", "Turns the fan on for 1 minute" },
	{ "On10", "Turns the fan on for 10 minutes" },
	{ "On20", "Turns the fan on for 20 minutes" },
	{ "On30", "Turns the fan on for 30 minutes" },
	{ "Off", "Turns the fan off and clears the run timer" },
}};

///
/// <summary>
/// Formats the current air quality score (2 decimal places) and discrete tier name for logging.
/// </summary>
/// <param name="score">Fractional air quality score.</param>
/// <param name="tier">Discrete tier index (0-5).</param>
/// <returns>Formatted tier description.</returns>
///
std::string formatTier(float score, int8_t tier)
{
	std::ostringstream out;
	out << std::fixed << std::setprecision(2) << score;
	if (tier >= 0 && tier < AirQualityTiers::NUM_TIERS)
	{
		out << " (" << AirQualityTiers::TIER_NAMES[tier] << ")";
	}

	return out.str();
}

///
/// <summary>
/// Formats an air quality score to 1 decimal place for logging.
/// </summary>
/// <param name="score">Fractional air quality score.</param>
/// <returns>Formatted score string.</returns>
///
std::string formatScore(float score)
{
	std::ostringstream out;
	out << std::fixed << std::setprecision(1) << score;
	return out.str();
}

///
/// <summary>
/// Sets the fan control pin and LED state and logs transitions.
/// </summary>
/// <param name="on">True to turn the fan on (HIGH); false to turn it off (LOW).</param>
/// <param name="reason">Description of why the fan state changed.</param>
///
void setFanState(bool on, const std::string& reason)
{
	if (on && !fanOn)
	{
		fanOnStopwatch.reset();
		fanOnStopwatch.start();
	}
	else if (!on)
	{
		fanOnStopwatch.stop();
	}

	fanOn = on;
	digitalWrite(FAN_PIN, fanOn ? HIGH : LOW);
	if (fanOn)
	{
		sketch.arduino.led.turnOn();
	}
	else
	{
		sketch.arduino.led.turnOff();
	}
	Logger.log(std::string("Fan turned ") + (fanOn ? "ON" : "OFF") + " (" + reason + ")", LogSeverity::INFO, "Fan");
}

///
/// <summary>
/// Turns the fan on for the specified number of minutes via a manual command.
/// </summary>
/// <param name="minutes">Duration in minutes to run the fan.</param>
///
void startManualFanRun(uint16_t minutes)
{
	fanTimer.setDurationMs(minutes * 60UL * 1000UL);
	std::string durationText = std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
	setFanState(true, "manual " + durationText);
	DeviceServerClient::respond(("ON (" + durationText + ")").c_str());
}

///
/// <summary>
/// Loads the persisted trigger quality level from Preferences, falling back to DEFAULT_TRIGGER_LEVEL.
/// </summary>
///
void loadTriggerLevel()
{
	sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
	float saved = sketch.arduino.preferences.getFloat(TRIGGER_LEVEL_PREF_KEY, DEFAULT_TRIGGER_LEVEL);
	sketch.arduino.preferences.end();

	if (!std::isnan(saved) && saved >= 0.0f && saved <= static_cast<float>(AirQualityTiers::NUM_TIERS - 1))
	{
		triggerLevel = saved;
	}
}

///
/// <summary>
/// Persists the trigger quality level to Preferences.
/// </summary>
/// <param name="level">New trigger quality level.</param>
///
void saveTriggerLevel(float level)
{
	triggerLevel = level;
	sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
	sketch.arduino.preferences.putFloat(TRIGGER_LEVEL_PREF_KEY, triggerLevel);
	sketch.arduino.preferences.end();
}

///
/// <summary>
/// Handles the fan control commands registered via sketch.setCommandHandler().
/// </summary>
/// <param name="command">Full command text received from the DeviceServer.</param>
///
void onFanCommand(const char* command)
{
	std::istringstream stream(command);
	std::string verb;
	stream >> verb;

	if (strcasecmp(verb.c_str(), "GetState") == 0)
	{
		std::string state = fanOn ? "ON" : "OFF";
		if (fanOn)
		{
			uint32_t remainingSecs = std::ceil(fanTimer.remaining());
			uint32_t mins = remainingSecs / 60;
			uint32_t secs = remainingSecs % 60;
			state += " (" + std::to_string(mins) + "m " + std::to_string(secs) + "s remaining)";
		}
		std::string tierText = currentTier >= 0 ? formatTier(currentScore, currentTier) : "none";
		DeviceServerClient::respond((state + ", tier = " + tierText + ", trigger = " + formatScore(triggerLevel)).c_str());
	}
	else if (strcasecmp(verb.c_str(), "GetTriggerLevel") == 0)
	{
		DeviceServerClient::respond(("triggerLevel = " + formatScore(triggerLevel)).c_str());
	}
	else if (strcasecmp(verb.c_str(), "SetTriggerLevel") == 0)
	{
		float level = NAN;
		std::string extra;
		constexpr float MAX_LEVEL = AirQualityTiers::NUM_TIERS - 1;
		if (!(stream >> level) || (stream >> extra) || std::isnan(level) || level < 0.0f || level > MAX_LEVEL)
		{
			DeviceServerClient::respond("Error: level must be a number between 0.0 and 5.0");
			return;
		}
		saveTriggerLevel(level);
		DeviceServerClient::respond(("Saved triggerLevel = " + formatScore(triggerLevel)).c_str());
	}
	else if (strcasecmp(verb.c_str(), "On") == 0)
	{
		int minutes = 1;
		std::string extra;
		if (stream >> minutes)
		{
			if (minutes <= 0 || (stream >> extra))
			{
				DeviceServerClient::respond("Error: minutes must be a positive integer");
				return;
			}
		}
		else if (!stream.eof())
		{
			DeviceServerClient::respond("Error: minutes must be a positive integer");
			return;
		}
		startManualFanRun(minutes);
	}
	else if (strcasecmp(verb.c_str(), "On1") == 0)
	{
		startManualFanRun(1);
	}
	else if (strcasecmp(verb.c_str(), "On10") == 0)
	{
		startManualFanRun(10);
	}
	else if (strcasecmp(verb.c_str(), "On20") == 0)
	{
		startManualFanRun(20);
	}
	else if (strcasecmp(verb.c_str(), "On30") == 0)
	{
		startManualFanRun(30);
	}
	else if (strcasecmp(verb.c_str(), "Off") == 0)
	{
		setFanState(false, "command Off");
		DeviceServerClient::respond("OFF");
	}
	else
	{
		DeviceServerClient::respond((std::string("Unhandled command: ") + command).c_str());
	}
}

void setup()
{
	SerialX::begin();

	pinMode(FAN_PIN, OUTPUT);
	digitalWrite(FAN_PIN, LOW);

	loadTriggerLevel();

	sketch.setCommandHandler(onFanCommand, FAN_COMMANDS.data(), FAN_COMMANDS.size());
	sketch.beginBanner();

	sketch.resolveSiteLocation();
	airQualityTopic = std::string(AIR_QUALITY_TOPIC_PREFIX) + "/" + sketch.siteLocation();

	sketch.beginConnect();

	sketch.beginTelemetry(airQualityTopic.c_str());
	sketch.getClient()->onSample([](const std::string& topic, double value, int64_t dtMicros)
	{
		(void)topic;
		(void)dtMicros;

		if (std::isnan(value))
		{
			return;
		}

		currentScore = static_cast<float>(value);
		int tier = static_cast<int>(std::floor(value));
		currentTier = static_cast<int8_t>(std::clamp(tier, 0, static_cast<int>(AirQualityTiers::NUM_TIERS - 1)));

		if (currentScore >= triggerLevel)
		{
			constexpr float BAD_AIR_RUN_S = BAD_AIR_RUN_M * 60.0f;
			if (!fanOn || fanTimer.remaining() < BAD_AIR_RUN_S)
			{
				fanTimer.setDurationMs(BAD_AIR_RUN_M * 60UL * 1000UL);
			}

			if (!fanOn)
			{
				setFanState(true, "trigger quality level = " + formatScore(currentScore));
			}
		}
	});

	sketch.completeInitialization();
}

void loop()
{
	sketch.loop();

	if (fanOn && fanTimer.ready())
	{
		uint32_t onMins = static_cast<uint32_t>(std::round(fanOnStopwatch.elapsedSecs() / 60.0));
		std::string reason = "on for " + std::to_string(onMins) + " mins";
		if (!std::isnan(currentScore))
		{
			reason += ", quality level = " + formatScore(currentScore);
		}
		setFanState(false, reason);
	}
}
