namespace LittleBearPublisher;

/// <summary>
/// Formats timestamps for display, using "Today" and "Yesterday" where they apply.
/// </summary>
internal static class DateText
{
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
}
