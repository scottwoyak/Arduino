//
// Air Subscriber Fan
//
// Subscribes to the AirQuality telemetry topic published by Air_Monitor_Display
// (AirQuality/site/location, reporting the worst 0-5 air quality tier across all sensors)
// and drives a fan control pin (A0, active HIGH) whenever the tier drops outside of the
// green (Good, tier 0) air quality tier.
//
// Behavior:
// - Prompts for the target site and location on first boot (or when buttonA is held at
//   startup) and saves the selection in Preferences.
// - Turns the fan pin HIGH immediately when a received AirQuality tier is >= 1 (outside
//   the green/Good tier).
// - While the fan is running, checks the latest tier on a 1-minute interval and keeps
//   the fan pin HIGH until the tier returns to 0 (green).
//

#include <Arduino.h>
#include <array>
#include <cmath>
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

constexpr uint8_t FAN_PIN = A0;
constexpr float CHECK_INTERVAL_S = 60.0f;

// Site choices offered when selecting which air monitor location to subscribe to;
// location is entered as free text. Edit to match your publishers.
constexpr std::array<const char*, 2> SITE_OPTIONS = { "Bragg", "Lake" };

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
TimerSecs checkTimer(CHECK_INTERVAL_S);

std::string airQualityTopic;
int8_t currentTier = -1;
bool fanOn = false;

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
/// Sets the fan control pin state and logs transitions.
/// </summary>
/// <param name="on">True to turn the fan on (HIGH); false to turn it off (LOW).</param>
/// <param name="reason">Description of why the fan state changed.</param>
///
void setFanState(bool on, const std::string& reason)
{
	fanOn = on;
	digitalWrite(FAN_PIN, fanOn ? HIGH : LOW);
	Logger.log(std::string("Fan turned ") + (fanOn ? "ON" : "OFF") + " (" + reason + ")", LogSeverity::INFO, "Fan");
}

void setup()
{
	SerialX::begin();

	pinMode(FAN_PIN, OUTPUT);
	digitalWrite(FAN_PIN, LOW);

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

		if (!fanOn && currentTier >= 1)
		{
			checkTimer.reset();
			setFanState(true, "tier = " + formatTier(currentTier));
		}
	});

	sketch.completeInitialization();
}

void loop()
{
	sketch.loop();

	if (!fanOn)
	{
		return;
	}

	if (checkTimer.ready() && currentTier == 0)
	{
		setFanState(false, "tier = " + formatTier(currentTier));
	}
}
