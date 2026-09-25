#pragma once

#include <span>

// Requires WiFiSettings.h to have already been included (for INFLUXDB_BUCKET).

///
/// <summary>
/// Fixed InfluxDB site/location entry, e.g. one gate, wind, or wave site. Used by
/// SketchConfig::influx (both as the single fixed entry via context, and within the
/// selectable prompts table) so the site table format and Preferences persistence
/// logic stay identical across Monitor and Publisher sketches.
/// </summary>
///
struct InfluxContext
{
   /// <summary>InfluxDB bucket this site's points are written to.</summary>
   const char* bucket = nullptr;

   /// <summary>Value for the "site" tag attached to points logged for this site.</summary>
   const char* site = nullptr;

   /// <summary>Value for the "location" tag attached to points logged for this site.</summary>
   const char* location = nullptr;
};

///
/// <summary>
/// All sketch-wide InfluxDB settings for a Monitor/Publisher sketch: measurement
/// names, post cadence, rolling-average sample count, and site selection (either a
/// single fixed site, or a table of selectable sites). Grouped together here (rather
/// than left flat on SketchConfig) since these values apply uniformly across every
/// entry in prompts - unlike bucket/site/location/sensor, which vary per entry (see
/// InfluxContext).
/// </summary>
///
struct InfluxConfig
{
   /// <summary>Fixed InfluxDB site+location entry used when prompts is empty (no selection prompt). Ignored if prompts is non-empty. If promptForContext is set (Monitor only), bucket/site/location are prompted for instead.</summary>
   InfluxContext context = { INFLUXDB_BUCKET, nullptr, nullptr };

   /// <summary>Table of selectable InfluxDB site+location entries. Leave empty for a sketch with a single fixed site/location (see context) instead of a user-selectable table.</summary>
   std::span<const InfluxContext> prompts;

   /// <summary>Influx measurement name used for the standard enclosure/CPU points and any points added via addPoint().</summary>
   const char* measurement = "Sensors";

   /// <summary>How often (in seconds) queued Influx points are posted/flushed.</summary>
   uint16_t intervalS = 60;

   /// <summary>Number of samples averaged for the standard rolling-average enclosure temperature/humidity fields.</summary>
   size_t rollingSamples = 10;

   /// <summary>Monitor only: if true, prompts over Serial (or loads the saved values from Preferences) for a bucket (chosen from Monitor's shared BUCKET_OPTIONS list), a site (chosen from Monitor's shared SITE_OPTIONS list), and a free-text location, instead of using context.</summary>
   bool promptForContext = false;

   /// <summary>If true (the default),
   bool batchPoints = true;

   /// <summary>If true, capture and upload rolling-averaged enclosure temperature/humidity to InfluxDB.</summary>
   bool includeEnclosureTemp = false;

   /// <summary>If true, capture and upload the ESP32 CPU temperature to InfluxDB.</summary>
   bool includeCpuTemp = false;
};
