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
    public string Version { get; set; } = "";
    public DateTime Built { get; set; }
    public string? BuildTime { get; set; }
    public string BinPath { get; set; } = "";
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
    /// Human-readable status text shown in the Status column.
    /// </summary>
    public string StatusText => !string.IsNullOrEmpty(Detail)
        ? Detail
        : Status switch
        {
            FirmwareStatus.Ready => "Ready",
            FirmwareStatus.UpToDate => "",
            FirmwareStatus.Skipped => "Skipped",
            _ => "",
        };

    /// <summary>
    /// Identifies this exact build so a notification is only shown once per build.
    /// </summary>
    public string Key => $"{Firmware}|{Version}|{BuildTime ?? Built.Ticks.ToString()}";
}
