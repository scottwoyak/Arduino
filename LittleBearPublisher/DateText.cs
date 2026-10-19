using System.Globalization;

namespace LittleBearPublisher;

/// <summary>
/// Formats timestamps for display, using "Today" and "Yesterday" where they apply.
/// </summary>
internal static class DateText
{
    private static readonly string[] BuildTimeFormats =
    [
        "MMM dd yyyy HH:mm:ss",
        "MMM  d yyyy HH:mm:ss",
        "MMM d yyyy HH:mm:ss",
    ];

    /// <summary>
    /// Formats a local time as "Today 3:41 PM", "Yesterday 3:41 PM", or the general short date and time.
    /// </summary>
    /// <param name="time">Local time to format.</param>
    public static string Format(DateTime time)
    {
        DateTime today = DateTime.Today;

        if (time.Date == today)
        {
            return $"Today {time:t}";
        }

        if (time.Date == today.AddDays(-1))
        {
            return $"Yesterday {time:t}";
        }

        return time.ToString("g");
    }

    /// <summary>
    /// Parses a build timestamp string (e.g. "Oct 17 2026 12:46:41") and formats it with
    /// "Today" and "Yesterday" where they apply, or returns the original string if unparseable.
    /// </summary>
    /// <param name="buildTime">Build timestamp string to format.</param>
    public static string Format(string? buildTime)
    {
        if (string.IsNullOrWhiteSpace(buildTime))
        {
            return "";
        }

        return TryParse(buildTime, out DateTime parsed)
            ? Format(parsed)
            : buildTime;
    }

    /// <summary>
    /// Attempts to parse a C++ __DATE__ / __TIME__ build timestamp string or general date string.
    /// </summary>
    public static bool TryParse(string? buildTime, out DateTime result)
    {
        if (string.IsNullOrWhiteSpace(buildTime))
        {
            result = default;
            return false;
        }

        if (DateTime.TryParseExact(buildTime, BuildTimeFormats, CultureInfo.InvariantCulture, DateTimeStyles.AssumeLocal, out result))
        {
            return true;
        }

        return DateTime.TryParse(buildTime, CultureInfo.InvariantCulture, DateTimeStyles.AssumeLocal, out result);
    }
}
