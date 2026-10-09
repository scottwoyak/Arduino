using System.Text;
using System.Text.RegularExpressions;

namespace LittleBearPublisher;

/// <summary>
/// Finds compiled firmware under each OTA-enabled sketch's build folder, using the same
/// rules as Tools\Publish-AllToHub.ps1.
/// </summary>
internal sealed class FirmwareScanner
{
    private static readonly string[] KnownBoardIds =
    [
        "ADAFRUIT_FEATHER_M0",
        "ADAFRUIT_FEATHER_ESP32S3_TFT",
        "WAVESHARE_ESP32_S3_ZERO_SENSORS",
        "WAVESHARE_ESP32_S3_ZERO",
        "WAVESHARE_ESP32S3_TOUCH_LCD_43",
        "HOSYOND_ESP32_S3_VIEWER",
        "ESP32S3_DEV_PLAYGROUND",
    ];

    private static readonly Regex LibraryVersionRegex = new("LIBRARY_VERSION\\s*=\\s*\"([^\"]+)\"");
    private static readonly Regex MakeVersionRegex = new("MakeVersion\\(\"([^\"]+)\"\\)");
    private static readonly Regex EnableOtaRegex = new(@"enableOTA\s*=\s*true");
    private static readonly Regex BuildTimeRegex = new(@"BUILD@([A-Z][a-z]{2} [ 0-9]\d \d{4} \d\d:\d\d:\d\d)");

    private readonly string _repoRoot;

    public FirmwareScanner(string repoRoot)
    {
        _repoRoot = repoRoot;
    }

    /// <summary>
    /// Scans every OTA-enabled sketch and classifies its newest binary per board.
    /// </summary>
    /// <param name="hub">Used to check which versions the server already has.</param>
    /// <param name="reupload">When true, skips the server check so everything current is Ready.</param>
    public async Task<List<FirmwareItem>> ScanAsync(HubClient hub, bool reupload)
    {
        var items = new List<FirmwareItem>();

        string libraryHeader = Path.Combine(_repoRoot, "libraries", "Woyak", "LibraryVersion.h");
        var libraryMatch = LibraryVersionRegex.Match(await File.ReadAllTextAsync(libraryHeader));
        if (!libraryMatch.Success)
        {
            throw new InvalidOperationException($"Could not parse LIBRARY_VERSION from '{libraryHeader}'.");
        }
        string libraryVersion = libraryMatch.Groups[1].Value;
        DateTime libraryVersionTime = File.GetLastWriteTime(libraryHeader);

        foreach (string dir in Directory.EnumerateDirectories(_repoRoot).OrderBy(d => d, StringComparer.OrdinalIgnoreCase))
        {
            string name = Path.GetFileName(dir);
            string inoPath = Path.Combine(dir, name + ".ino");
            if (!File.Exists(inoPath))
            {
                continue;
            }

            string inoText = await File.ReadAllTextAsync(inoPath);
            if (!EnableOtaRegex.IsMatch(inoText) && !inoText.Contains("TempMonitorSketch"))
            {
                continue;
            }

            string buildsRoot = Path.Combine(dir, "build");
            string publishRoot = Path.Combine(buildsRoot, "_publish") + Path.DirectorySeparatorChar;
            var bins = new List<FileInfo>();
            if (Directory.Exists(buildsRoot))
            {
                bins = Directory.EnumerateFiles(buildsRoot, name + ".ino.bin", SearchOption.AllDirectories)
                    .Where(f => !f.StartsWith(publishRoot, StringComparison.OrdinalIgnoreCase))
                    .Select(f => new FileInfo(f))
                    .OrderByDescending(f => f.LastWriteTime)
                    .ToList();
            }

            if (bins.Count == 0)
            {
                items.Add(new FirmwareItem { Sketch = name, Status = FirmwareStatus.Skipped, Detail = "no build output" });
                continue;
            }

            var versionMatch = MakeVersionRegex.Match(inoText);
            if (!versionMatch.Success)
            {
                items.Add(new FirmwareItem { Sketch = name, Status = FirmwareStatus.Skipped, Detail = "no MakeVersion(...) literal" });
                continue;
            }
            string version = $"{versionMatch.Groups[1].Value}.{libraryVersion}";

            DateTime sourceTime = File.GetLastWriteTime(inoPath);
            if (libraryVersionTime > sourceTime)
            {
                sourceTime = libraryVersionTime;
            }

            var seenBoards = new HashSet<string>();
            foreach (var bin in bins)
            {
                string text;
                try
                {
                    text = Encoding.Latin1.GetString(await File.ReadAllBytesAsync(bin.FullName));
                }
                catch (IOException)
                {
                    items.Add(new FirmwareItem { Sketch = name, Status = FirmwareStatus.Skipped, Detail = $"{bin.FullName} is still being written" });
                    continue;
                }

                string? boardId = GetBoardId(text);
                if (boardId == null)
                {
                    items.Add(new FirmwareItem { Sketch = name, Status = FirmwareStatus.Skipped, Detail = $"unknown board id in {bin.FullName}" });
                    continue;
                }
                if (!seenBoards.Add(boardId))
                {
                    continue;
                }

                var buildMatch = BuildTimeRegex.Match(text);
                var item = new FirmwareItem
                {
                    Sketch = name,
                    Board = boardId,
                    Version = version,
                    Built = bin.LastWriteTime,
                    BuildTime = buildMatch.Success ? buildMatch.Groups[1].Value : null,
                    BinPath = bin.FullName,
                };

                if (bin.LastWriteTime < sourceTime)
                {
                    item.Status = FirmwareStatus.Stale;
                    item.Detail = $"last build: {DateText.Format(bin.LastWriteTime)}";
                }
                else if (!reupload && await hub.IsAlreadyPublishedAsync(item))
                {
                    item.Status = FirmwareStatus.UpToDate;
                    item.Detail = "already on server";
                }
                else
                {
                    item.Status = FirmwareStatus.Ready;
                }
                items.Add(item);
            }
        }

        return items;
    }

    private static string? GetBoardId(string binText)
    {
        foreach (string candidate in KnownBoardIds)
        {
            if (Regex.IsMatch(binText, $"[^A-Za-z0-9_]{candidate}[^A-Za-z0-9_]"))
            {
                return candidate;
            }
        }
        return null;
    }
}
