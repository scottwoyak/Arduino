#pragma once

#include <string>

///
/// <summary>
/// Central build number for the shared Woyak library. Bump this whenever a change to
/// any shared library header should cause every OTA-enabled sketch's compiled VERSION
/// to change, forcing a republish/OTA update across all sketches without having to
/// manually edit each sketch's own version literal. Each sketch combines its own
/// version (e.g. "2.4") with this build number to form the full compiled version
/// (e.g. "2.4.1"). Defined as a preprocessor macro (not constexpr) so it can
/// participate in compile-time string literal concatenation inside MakeVersion(...).
/// </summary>
///
#define LIBRARY_VERSION "184"

///
/// <summary>
/// Combines a sketch's own version literal with the shared LIBRARY_VERSION to form
/// the full compiled version string (e.g. "2.4" + "1" -> "2.4.1"). Use this to
/// initialize each sketch's VERSION constant so that bumping LIBRARY_VERSION alone
/// changes every sketch's compiled version.
///
/// Implemented as a macro so the "VERSION@" + version string is concatenated at
/// compile time into a single string literal (the same trick DeviceServerClient's
/// BUILD_TIME marker uses for "BUILD@" __DATE__ " " __TIME__). This embeds the
/// marker verbatim in the compiled binary's data, so tools (e.g. LittleBearPublisher)
/// can scan a .bin file and recover the exact version it was built with, even if the
/// source/LIBRARY_VERSION has since changed. The marker prefix is stripped at runtime
/// so sketches still see the clean version string (e.g. "2.4.1").
/// </summary>
/// <param name="sketchVersion">This sketch's own version string literal (e.g. "2.4").</param>
/// <returns>The combined version string (e.g. "2.4.1"), valid for the lifetime of the program.</returns>
///
#define MakeVersion(sketchVersion) (_StripVersionMarker("VERSION@" sketchVersion "." LIBRARY_VERSION))

/// <summary>
/// Strips the compile-time "VERSION@" marker prefix from a string produced by the
/// MakeVersion(...) macro, returning the clean version string. Not intended to be
/// called directly; use the MakeVersion(...) macro instead.
/// </summary>
inline const char* _StripVersionMarker(const char* markedVersion)
{
   constexpr size_t MARKER_PREFIX_LEN = 8; // strlen("VERSION@")
   return markedVersion + MARKER_PREFIX_LEN;
}
