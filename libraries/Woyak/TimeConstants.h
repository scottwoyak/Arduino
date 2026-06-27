#pragma once

/// <summary>
/// Common time unit conversion constants.
/// </summary>
/// <remarks>
/// These macros provide convenient conversions between different time units,
/// useful for creating readable time durations in sketches and libraries.
/// </remarks>

/// <summary>Seconds per second (1)</summary>
#define SECS_SEC (1)
/// <summary>Seconds per minute (60)</summary>
#define SECS_MIN (60 * SECS_SEC)
/// <summary>Seconds per hour (3600)</summary>
#define SECS_HOUR (60 * SECS_MIN)
/// <summary>Seconds per day (86400)</summary>
#define SECS_DAY (24 * SECS_HOUR)
/// <summary>Seconds per year (31536000)</summary>
#define SECS_YEAR (365 * SECS_DAY)

/// <summary>Milliseconds per second (1000)</summary>
#define MILLIS_SEC (1000)
/// <summary>Milliseconds per minute (60000)</summary>
#define MILLIS_MIN (60 * MILLIS_SEC)
/// <summary>Milliseconds per hour (3600000)</summary>
#define MILLIS_HOUR (60 * MILLIS_MIN)
/// <summary>Milliseconds per day (86400000)</summary>
#define MILLIS_DAY (24 * MILLIS_HOUR)
/// <summary>Milliseconds per year (31536000000)</summary>
#define MILLIS_YEAR (365 * MILLIS_DAY)

/// <summary>Microseconds per second (1000000)</summary>
#define MICROS_SEC (1000000)
/// <summary>Microseconds per minute (60000000)</summary>
#define MICROS_MIN (60 * MICROS_SEC)
/// <summary>Microseconds per hour (3600000000)</summary>
#define MICROS_HOUR (60 * MICROS_MIN)
/// <summary>Microseconds per day (86400000000)</summary>
#define MICROS_DAY (24 * MICROS_HOUR)
