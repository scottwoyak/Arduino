namespace LittleBearPublisher;

/// <summary>
/// Outcome of evaluating one compiled firmware binary.
/// </summary>
internal enum FirmwareStatus
{
    Ready,
    UpToDate,
    Stale,
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

    public string Firmware => $"{Sketch}.{Board}";

    /// <summary>
    /// Identifies this exact build so a notification is only shown once per build.
    /// </summary>
    public string Key => $"{Firmware}|{Version}|{BuildTime ?? Built.Ticks.ToString()}";
}
