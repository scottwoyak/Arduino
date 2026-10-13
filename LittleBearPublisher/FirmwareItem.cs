namespace LittleBearPublisher;

/// <summary>
/// Outcome of evaluating one compiled firmware binary.
/// </summary>
internal enum FirmwareStatus
{
    Ready,
    UpToDate,
    Skipped,
}

/// <summary>
/// One compiled firmware binary (newest per sketch and board) and what to do with it.
/// </summary>
internal sealed class FirmwareItem
{
    public required string Sketch { get; init; }
    public string Board { get; init; } = "";
    public string Version { get; init; } = "";
    public DateTime Built { get; init; }
    public string? BuildTime { get; init; }
    public string BinPath { get; init; } = "";
    public FirmwareStatus Status { get; set; }
    public string Detail { get; set; } = "";
    public string? ServerVersion { get; set; }
    public string? ServerBuildTime { get; set; }

    /// <summary>
    /// True when the binary was built before the source (or shared library) last changed.
    /// This is informational only; it does not affect whether the binary can be uploaded.
    /// </summary>
    public bool IsStale { get; set; }

    public string Firmware => $"{Sketch}.{Board}";

    /// <summary>
    /// Version and build date currently published on the server, formatted for display.
    /// </summary>
    public string ServerInfo => ServerVersion is null
        ? ""
        : ServerBuildTime is null ? ServerVersion : $"{ServerVersion} ({ServerBuildTime})";

    /// <summary>
    /// Identifies this exact build so a notification is only shown once per build.
    /// </summary>
    public string Key => $"{Firmware}|{Version}|{BuildTime ?? Built.Ticks.ToString()}";
}
