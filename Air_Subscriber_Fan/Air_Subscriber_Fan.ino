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
// - Turns the fan pin HIGH immediately when a received AirQuality score is >= triggerOn
//   and keeps it on until the score drops <= triggerOff and FAN_OFF_DELAY_M minutes
//   have elapsed.
// - Exposes DeviceServer commands (GetState, GetTriggerOn, SetTriggerOn, GetTriggerOff,
//   SetTriggerOff, On [minutes], Off) to inspect or manually run/stop the fan.
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

const auto VERSION = MakeVersion("1.3");
constexpr auto SKETCH_NAME = "Air_Subscriber_Fan";
constexpr auto PREFERENCES_NAMESPACE = "AirSubFan";
constexpr auto AIR_QUALITY_TOPIC_PREFIX = "AirQuality";

constexpr uint8_t FAN_PIN = 5;
constexpr uint8_t FAN_OFF_DELAY_M = 0;
constexpr float DEFAULT_TRIGGER_ON = 1.0f;
constexpr float DEFAULT_TRIGGER_OFF = 0.8f;
constexpr auto TRIGGER_ON_PREF_KEY = "triggerOn";
constexpr auto TRIGGER_OFF_PREF_KEY = "triggerOff";

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
float triggerOn = DEFAULT_TRIGGER_ON;
float triggerOff = DEFAULT_TRIGGER_OFF;
bool fanOn = false;

///
/// <summary>
/// Commands registered with DeviceServerClient, listed by "Help" after the built-ins.
/// </summary>
///
constexpr uint8_t NUM_FAN_COMMANDS = 7;
constexpr std::array<DeviceServerClient::CommandInfo, NUM_FAN_COMMANDS> FAN_COMMANDS = {{
	{ "GetState", "Reports the current fan state, remaining run timer, and air quality tier", { "gs" } },
	{ "GetTriggerOn", "Reports the air quality score threshold that turns the fan on", { "gton" } },
	{
		"SetTriggerOn",
		"Sets and persists the air quality score threshold (0.0-5.0) that turns the fan on",
		{ "ston" },
		{
			{ "level", "Trigger-on air quality score (0.0-5.0)", true },
		},
	},
	{ "GetTriggerOff", "Reports the air quality score threshold that turns the fan off", { "gtoff" } },
	{
		"SetTriggerOff",
		"Sets and persists the air quality score threshold (0.0-5.0) that turns the fan off",
		{ "stoff" },
		{
			{ "level", "Trigger-off air quality score (0.0-5.0)", true },
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
		sketch.arduino.led2.turnOn();
	}
	else
	{
		sketch.arduino.led2.turnOff();
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
/// Loads the persisted triggerOn and triggerOff quality levels from Preferences, falling
/// back to DEFAULT_TRIGGER_ON and DEFAULT_TRIGGER_OFF.
/// </summary>
///
void loadTriggerLevels()
{
	constexpr float MAX_LEVEL = AirQualityTiers::NUM_TIERS - 1;

	sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
	float savedOn = sketch.arduino.preferences.getFloat(TRIGGER_ON_PREF_KEY, DEFAULT_TRIGGER_ON);
	float savedOff = sketch.arduino.preferences.getFloat(TRIGGER_OFF_PREF_KEY, DEFAULT_TRIGGER_OFF);
	sketch.arduino.preferences.end();

	if (!std::isnan(savedOn) && savedOn >= 0.0f && savedOn <= MAX_LEVEL)
	{
		triggerOn = savedOn;
	}

	if (!std::isnan(savedOff) && savedOff >= 0.0f && savedOff <= MAX_LEVEL)
	{
		triggerOff = savedOff;
	}
}

///
/// <summary>
/// Persists the trigger-on quality level to Preferences.
/// </summary>
/// <param name="level">New trigger-on quality level.</param>
///
void saveTriggerOn(float level)
{
	triggerOn = level;
	sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
	sketch.arduino.preferences.putFloat(TRIGGER_ON_PREF_KEY, triggerOn);
	sketch.arduino.preferences.end();
}

///
/// <summary>
/// Persists the trigger-off quality level to Preferences.
/// </summary>
/// <param name="level">New trigger-off quality level.</param>
///
void saveTriggerOff(float level)
{
	triggerOff = level;
	sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
	sketch.arduino.preferences.putFloat(TRIGGER_OFF_PREF_KEY, triggerOff);
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
		if (fanOn && fanTimer.getDurationMs() > 0)
		{
			uint32_t remainingSecs = std::ceil(fanTimer.remaining());
			uint32_t mins = remainingSecs / 60;
			uint32_t secs = remainingSecs % 60;
			state += " (" + std::to_string(mins) + "m " + std::to_string(secs) + "s remaining)";
		}
		std::string tierText = currentTier >= 0 ? formatTier(currentScore, currentTier) : "none";
		DeviceServerClient::respond((state + ", Air Quality = " + tierText + ", triggerOn = " + formatScore(triggerOn) + ", triggerOff = " + formatScore(triggerOff)).c_str());
	}
	else if (strcasecmp(verb.c_str(), "GetTriggerOn") == 0)
	{
		DeviceServerClient::respond(("triggerOn = " + formatScore(triggerOn)).c_str());
	}
	else if (strcasecmp(verb.c_str(), "SetTriggerOn") == 0)
	{
		float level = NAN;
		std::string extra;
		constexpr float MAX_LEVEL = AirQualityTiers::NUM_TIERS - 1;
		if (!(stream >> level) || (stream >> extra) || std::isnan(level) || level < 0.0f || level > MAX_LEVEL)
		{
			DeviceServerClient::respond("Error: level must be a number between 0.0 and 5.0");
			return;
		}
		saveTriggerOn(level);
		DeviceServerClient::respond(("Saved triggerOn = " + formatScore(triggerOn)).c_str());
	}
	else if (strcasecmp(verb.c_str(), "GetTriggerOff") == 0)
	{
		DeviceServerClient::respond(("triggerOff = " + formatScore(triggerOff)).c_str());
	}
	else if (strcasecmp(verb.c_str(), "SetTriggerOff") == 0)
	{
		float level = NAN;
		std::string extra;
		constexpr float MAX_LEVEL = AirQualityTiers::NUM_TIERS - 1;
		if (!(stream >> level) || (stream >> extra) || std::isnan(level) || level < 0.0f || level > MAX_LEVEL)
		{
			DeviceServerClient::respond("Error: level must be a number between 0.0 and 5.0");
			return;
		}
		saveTriggerOff(level);
		DeviceServerClient::respond(("Saved triggerOff = " + formatScore(triggerOff)).c_str());
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
	else if (strcasecmp(verb.c_str(), "Off") == 0)
	{
		fanTimer.setDurationMs(0);
		setFanState(false, "command Off");
		DeviceServerClient::respond("OFF");
	}
	else
	{
		DeviceServerClient::respond((std::string("Unhandled command: ") + command).c_str());
	}
}

///
/// <summary>
/// Turns the fan off and logs how long it was on and the current air quality score.
/// </summary>
///
void turnFanOffWithElapsedReason()
{
	fanTimer.setDurationMs(0);
	uint32_t onMins = static_cast<uint32_t>(std::round(fanOnStopwatch.elapsedSecs() / 60.0));
	std::string reason = "on for " + std::to_string(onMins) + " mins";
	if (!std::isnan(currentScore))
	{
		reason += ", quality level = " + formatScore(currentScore);
	}
	setFanState(false, reason);
}

void setup()
{
	SerialX::begin();

	pinMode(FAN_PIN, OUTPUT);
	digitalWrite(FAN_PIN, LOW);

	loadTriggerLevels();

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

		if (currentScore >= triggerOn || (fanOn && currentScore > triggerOff))
		{
			constexpr float FAN_OFF_DELAY_S = FAN_OFF_DELAY_M * 60.0f;
			if (FAN_OFF_DELAY_M > 0 && (!fanOn || fanTimer.remaining() < FAN_OFF_DELAY_S))
			{
				fanTimer.setDurationMs(FAN_OFF_DELAY_M * 60UL * 1000UL);
			}

			if (!fanOn && currentScore >= triggerOn)
			{
				setFanState(true, "trigger quality level = " + formatScore(currentScore));
			}
		}
		else if (fanOn && currentScore <= triggerOff && fanTimer.getDurationMs() == 0)
		{
			turnFanOffWithElapsedReason();
		}
	});

	sketch.completeInitialization();
}

void loop()
{
	sketch.loop();

	if (fanOn && fanTimer.getDurationMs() > 0 && fanTimer.ready())
	{
		if (std::isnan(currentScore) || currentScore <= triggerOff)
		{
			turnFanOffWithElapsedReason();
		}
		else if (FAN_OFF_DELAY_M == 0)
		{
			// Manual timer expired while air quality is still above triggerOff; keep the
			// fan running until the score drops to or below triggerOff.
			fanTimer.setDurationMs(0);
		}
	}
}
