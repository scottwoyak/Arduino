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
#include <array>
#include <cmath>
#include <sstream>
#include <string>

#include "ArduinoBoard.h"

#include "AirQualityTiers.h"
#include "LibraryVersion.h"
#include "Logger.h"
#include "SerialX.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "ViewerSketch.h"

#include "WiFiSettings.h"

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Air_Subscriber_Fan";
constexpr auto PREFERENCES_NAMESPACE = "AirSubFan";
constexpr auto AIR_QUALITY_TOPIC_PREFIX = "AirQuality";

constexpr uint8_t FAN_PIN = 5;
constexpr uint8_t BAD_AIR_RUN_M = 10;

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

std::string airQualityTopic;
int8_t currentTier = -1;
bool fanOn = false;

///
/// <summary>
/// Commands registered with DeviceServerClient, listed by "Help" after the built-ins.
/// </summary>
///
constexpr uint8_t NUM_FAN_COMMANDS = 7;
constexpr std::array<DeviceServerClient::CommandInfo, NUM_FAN_COMMANDS> FAN_COMMANDS = {{
	{ "GetState", "Reports the current fan state, remaining run timer, and air quality tier", { "gs" } },
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
/// Formats a tier index (0-5) with its tier name for logging.
/// </summary>
/// <param name="tier">Tier index.</param>
/// <returns>Formatted tier description.</returns>
///
std::string formatTier(int8_t tier)
{
	if (tier >= 0 && tier < AirQualityTiers::NUM_TIERS)
	{
		return std::to_string(tier) + " (" + AirQualityTiers::TIER_NAMES[tier] + ")";
	}

	return std::to_string(tier);
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
		std::string tierText = currentTier >= 0 ? formatTier(currentTier) : "none";
		DeviceServerClient::respond((state + ", tier = " + tierText).c_str());
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

		currentTier = lround(value);

		if (currentTier >= 1)
		{
			constexpr float BAD_AIR_RUN_S = BAD_AIR_RUN_M * 60.0f;
			if (!fanOn || fanTimer.remaining() < BAD_AIR_RUN_S)
			{
				fanTimer.setDurationMs(BAD_AIR_RUN_M * 60UL * 1000UL);
			}

			if (!fanOn)
			{
				setFanState(true, "tier = " + formatTier(currentTier));
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
		std::string reason = "timer expired";
		if (currentTier >= 0)
		{
			reason += ", tier = " + formatTier(currentTier);
		}
		setFanState(false, reason);
	}
}
