#pragma once

#include <string>

///
/// <summary>
/// Central build number for the shared Woyak library. Bump this whenever a change to
/// any shared library header should cause every OTA-enabled sketch's compiled VERSION
/// to change, forcing a republish/OTA update across all sketches without having to
/// manually edit each sketch's version.txt. Each sketch combines its own version.txt
/// (e.g. "2.4") with this build number to form the full compiled version (e.g. "2.4.1").
/// </summary>
///
constexpr auto LIBRARY_VERSION = "107";

///
/// <summary>
/// Combines a sketch's own version (from its version.txt) with the shared
/// LIBRARY_VERSION to form the full compiled version string (e.g. "2.4" + "1" ->
/// "2.4.1"). Use this to initialize each sketch's VERSION constant so that bumping
/// LIBRARY_VERSION alone changes every sketch's compiled version.
/// </summary>
/// <param name="sketchVersion">This sketch's own version, from version.txt (e.g. "2.4").</param>
/// <returns>The combined version string (e.g. "2.4.1"), valid for the lifetime of the program.</returns>
///
inline const char* MakeVersion(const char* sketchVersion)
{
   static std::string version = std::string(sketchVersion) + "." + LIBRARY_VERSION;
   return version.c_str();
}
