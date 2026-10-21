using System.Text;
using System.Text.RegularExpressions;

namespace LittleBearPublisher;

/// <summary>
/// Finds compiled firmware under each OTA-enabled sketch's build folder, using the same
/// rules as Tools\Publish-AllToHub.ps1.
/// </summary>
internal sealed class FirmwareScanner
{
    private static readonly string[] FallbackBoardIds =
    [
        "ADAFRUIT_FEATHER_M0",
        "ADAFRUIT_FEATHER_ESP32S3_TFT",
        "WAVESHARE_ESP32S3_ZERO_SENSORS",
        "WAVESHARE_ESP32S3_ZERO",
        "WAVESHARE_ESP32S3_TOUCH_LCD_43",
        "WAVESHARE_ESP32S3_TOUCH_LCD_19",
        "HOSYOND_ESP32S3_TOUCH_LCD_40",
        "ESP32S3_DEV_PLAYGROUND",
        "WAVESHARE_ESP32_S3_ZERO_SENSORS",
        "WAVESHARE_ESP32_S3_ZERO",
        "HOSYOND_ESP32_S3_VIEWER",
    ];

    private static readonly Regex BoardVariantIdRegex = new("ARDUINO_BOARD_VARIANT_ID\\s+\"([^\"]+)\"");
    private static readonly Regex BoardMarkerRegex = new(@"BOARD@([A-Za-z0-9_]+)");
    private static readonly Regex LibraryVersionRegex = new("LIBRARY_VERSION\\s+\"([^\"]+)\"");
    private static readonly Regex MakeVersionRegex = new("MakeVersion\\(\"([^\"]+)\"\\)");
    private static readonly Regex EnableOtaRegex = new(@"enableOTA\s*=\s*true");
    private static readonly Regex BuildTimeRegex = new(@"BUILD@([A-Z][a-z]{2} [ 0-9]\d \d{4} \d\d:\d\d:\d\d)");
    private static readonly Regex VersionMarkerRegex = new(@"VERSION@([0-9][0-9.]*)");

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
        IReadOnlyList<string> knownBoardIds = LoadKnownBoardIds();

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
            string vmBuildsRoot = Path.Combine(Path.GetTempPath(), "VMBuilds", name);
            var candidateBins = new List<FileInfo>();
            if (Directory.Exists(buildsRoot))
            {
                candidateBins.AddRange(
                    Directory.EnumerateFiles(buildsRoot, name + ".ino.bin", SearchOption.AllDirectories)
                        .Where(f => !f.StartsWith(publishRoot, StringComparison.OrdinalIgnoreCase))
                        .Select(f => new FileInfo(f)));
            }
            if (Directory.Exists(vmBuildsRoot))
            {
                candidateBins.AddRange(
                    Directory.EnumerateFiles(vmBuildsRoot, name + ".ino.bin", SearchOption.AllDirectories)
                        .Select(f => new FileInfo(f)));
            }
            var bins = candidateBins
                .OrderByDescending(f => f.LastWriteTime)
                .ToList();

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

            var seenBoards = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
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

                string? boardId = GetBoardId(text, knownBoardIds);
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
                var versionMarkerMatch = VersionMarkerRegex.Match(text);
                bool isStale = bin.LastWriteTime < sourceTime;

                // Prefer the version actually embedded in this binary at compile time over the
                // one recomputed from current source, since a stale (not-yet-rebuilt) binary may
                // no longer match current source/LIBRARY_VERSION content. When the marker is
                // missing (e.g. the binary predates this marker feature, or is otherwise stale),
                // don't claim it was built with the *current* LIBRARY_VERSION - that overstates
                // what we actually know. Show the shared build number as unknown instead.
                string resolvedVersion;
                if (versionMarkerMatch.Success)
                {
                    resolvedVersion = versionMarkerMatch.Groups[1].Value;
                }
                else if (isStale)
                {
                    resolvedVersion = $"{versionMatch.Groups[1].Value}.?";
                }
                else
                {
                    resolvedVersion = version;
                }

                string? buildTime = buildMatch.Success ? buildMatch.Groups[1].Value : null;
                DateTime built = DateText.TryParse(buildTime, out DateTime parsedBuildTime)
                    ? parsedBuildTime
                    : bin.LastWriteTime;

                var item = new FirmwareItem
                {
                    Sketch = name,
                    Board = boardId,
                    BoardName = FirmwareBuilder.GetBoardDisplayName(_repoRoot, boardId),
                    Version = resolvedVersion,
                    Built = built,
                    BuildTime = buildTime,
                    BinPath = bin.FullName,
                    IsStale = isStale,
                };

                (item.ServerVersion, item.ServerBuildTime) = await hub.GetLatestAsync(item.Firmware);

                if (!reupload && await hub.IsAlreadyPublishedAsync(item))
                {
                    item.Status = FirmwareStatus.UpToDate;
                }
                else
                {
                    item.Status = FirmwareStatus.Ready;
                }
                items.Add(item);
            }

            foreach (string configuredBoard in FirmwareBuilder.GetConfiguredBuildFolderBoards(_repoRoot, name))
            {
                if (!seenBoards.Add(configuredBoard))
                {
                    continue;
                }

                var missingItem = new FirmwareItem
                {
                    Sketch = name,
                    Board = configuredBoard,
                    BoardName = FirmwareBuilder.GetBoardDisplayName(_repoRoot, configuredBoard),
                    Status = FirmwareStatus.Skipped,
                    Detail = "no build output",
                };
                (missingItem.ServerVersion, missingItem.ServerBuildTime) = await hub.GetLatestAsync(missingItem.Firmware);
                items.Add(missingItem);
            }
        }

        return items;
    }

    /// <summary>
    /// Scans a single (sketch, board) target and updates <paramref name="item"/> in place
    /// with its newly compiled binary's version, build time, and publish readiness.
    /// </summary>
    public async Task RefreshItemAsync(FirmwareItem item, HubClient hub)
    {
        string libraryHeader = Path.Combine(_repoRoot, "libraries", "Woyak", "LibraryVersion.h");
        var libraryMatch = LibraryVersionRegex.Match(await File.ReadAllTextAsync(libraryHeader));
        if (!libraryMatch.Success)
        {
            return;
        }
        string libraryVersion = libraryMatch.Groups[1].Value;
        DateTime libraryVersionTime = File.GetLastWriteTime(libraryHeader);
        IReadOnlyList<string> knownBoardIds = LoadKnownBoardIds();

        string dir = Path.Combine(_repoRoot, item.Sketch);
        string inoPath = Path.Combine(dir, item.Sketch + ".ino");
        if (!File.Exists(inoPath))
        {
            return;
        }

        string inoText = await File.ReadAllTextAsync(inoPath);
        var versionMatch = MakeVersionRegex.Match(inoText);
        if (!versionMatch.Success)
        {
            return;
        }
        string version = $"{versionMatch.Groups[1].Value}.{libraryVersion}";

        DateTime sourceTime = File.GetLastWriteTime(inoPath);
        if (libraryVersionTime > sourceTime)
        {
            sourceTime = libraryVersionTime;
        }

        string buildsRoot = Path.Combine(dir, "build");
        string publishRoot = Path.Combine(buildsRoot, "_publish") + Path.DirectorySeparatorChar;
        string vmBuildsRoot = Path.Combine(Path.GetTempPath(), "VMBuilds", item.Sketch);
        var candidateBins = new List<FileInfo>();
        if (Directory.Exists(buildsRoot))
        {
            candidateBins.AddRange(
                Directory.EnumerateFiles(buildsRoot, item.Sketch + ".ino.bin", SearchOption.AllDirectories)
                    .Where(f => !f.StartsWith(publishRoot, StringComparison.OrdinalIgnoreCase))
                    .Select(f => new FileInfo(f)));
        }
        if (Directory.Exists(vmBuildsRoot))
        {
            candidateBins.AddRange(
                Directory.EnumerateFiles(vmBuildsRoot, item.Sketch + ".ino.bin", SearchOption.AllDirectories)
                    .Select(f => new FileInfo(f)));
        }

        var bins = candidateBins.OrderByDescending(f => f.LastWriteTime);

        foreach (var bin in bins)
        {
            string text;
            try
            {
                text = Encoding.Latin1.GetString(await File.ReadAllBytesAsync(bin.FullName));
            }
            catch (IOException)
            {
                continue;
            }

            string? boardId = GetBoardId(text, knownBoardIds);
            if (!string.Equals(boardId, item.Board, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var buildMatch = BuildTimeRegex.Match(text);
            var versionMarkerMatch = VersionMarkerRegex.Match(text);
            bool isStale = bin.LastWriteTime < sourceTime;

            string resolvedVersion;
            if (versionMarkerMatch.Success)
            {
                resolvedVersion = versionMarkerMatch.Groups[1].Value;
            }
            else if (isStale)
            {
                resolvedVersion = $"{versionMatch.Groups[1].Value}.?";
            }
            else
            {
                resolvedVersion = version;
            }

            string? buildTime = buildMatch.Success ? buildMatch.Groups[1].Value : null;
            item.Version = resolvedVersion;
            item.Built = DateText.TryParse(buildTime, out DateTime parsedBuildTime)
                ? parsedBuildTime
                : bin.LastWriteTime;
            item.BuildTime = buildTime;
            item.BinPath = bin.FullName;
            item.IsStale = isStale;
            item.Status = await hub.IsAlreadyPublishedAsync(item) ? FirmwareStatus.UpToDate : FirmwareStatus.Ready;
            return;
        }
    }

    /// <summary>
    /// Deletes all local build folders (under &lt;Sketch&gt;\build and %TEMP%\VMBuilds\&lt;Sketch&gt;)
    /// whose compiled binary matches <paramref name="item"/>'s board.
    /// </summary>
    public (bool Ok, string Detail) DeleteLocalBuilds(FirmwareItem item)
    {
        IReadOnlyList<string> knownBoardIds = LoadKnownBoardIds();
        string sketchDir = Path.Combine(_repoRoot, item.Sketch);
        string buildsRoot = Path.Combine(sketchDir, "build");
        string publishRoot = Path.Combine(buildsRoot, "_publish") + Path.DirectorySeparatorChar;
        string vmBuildsRoot = Path.Combine(Path.GetTempPath(), "VMBuilds", item.Sketch);

        var candidateBins = new List<string>();
        if (Directory.Exists(buildsRoot))
        {
            candidateBins.AddRange(
                Directory.EnumerateFiles(buildsRoot, item.Sketch + ".ino.bin", SearchOption.AllDirectories)
                    .Where(f => !f.StartsWith(publishRoot, StringComparison.OrdinalIgnoreCase)));
        }
        if (Directory.Exists(vmBuildsRoot))
        {
            candidateBins.AddRange(
                Directory.EnumerateFiles(vmBuildsRoot, item.Sketch + ".ino.bin", SearchOption.AllDirectories));
        }

        var dirsToDelete = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string? configuredFolder = FirmwareBuilder.GetBuildFolderName(_repoRoot, item.Board);
        if (!string.IsNullOrEmpty(configuredFolder))
        {
            string configuredPath = Path.Combine(buildsRoot, configuredFolder);
            if (Directory.Exists(configuredPath))
            {
                dirsToDelete.Add(configuredPath);
            }
        }

        foreach (string binPath in candidateBins)
        {
            try
            {
                string text = Encoding.Latin1.GetString(File.ReadAllBytes(binPath));
                string? boardId = GetBoardId(text, knownBoardIds);
                if (!string.Equals(boardId, item.Board, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                string? binDir = Path.GetDirectoryName(binPath);
                if (string.IsNullOrEmpty(binDir))
                {
                    continue;
                }

                // In VMBuilds, the binary is in ...\<Sketch>\<board>\Release\<Sketch>.ino.bin;
                // delete the <board> directory rather than leaving an empty parent.
                if (binDir.StartsWith(vmBuildsRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                {
                    string relative = Path.GetRelativePath(vmBuildsRoot, binDir);
                    string topSubFolder = relative.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)[0];
                    dirsToDelete.Add(Path.Combine(vmBuildsRoot, topSubFolder));
                }
                else if (binDir.StartsWith(buildsRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                {
                    string relative = Path.GetRelativePath(buildsRoot, binDir);
                    string topSubFolder = relative.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)[0];
                    dirsToDelete.Add(Path.Combine(buildsRoot, topSubFolder));
                }
                else
                {
                    dirsToDelete.Add(binDir);
                }
            }
            catch (Exception ex)
            {
                return (false, ex.Message);
            }
        }

        try
        {
            foreach (string dir in dirsToDelete)
            {
                if (Directory.Exists(dir))
                {
                    Directory.Delete(dir, recursive: true);
                }
            }

            return (true, $"Deleted {dirsToDelete.Count} folder(s)");
        }
        catch (Exception ex)
        {
            return (false, ex.Message);
        }
    }

    private IReadOnlyList<string> LoadKnownBoardIds()
    {
        var ids = new List<string>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        string boardIdHeader = Path.Combine(_repoRoot, "libraries", "Woyak", "ArduinoBoardId.h");
        if (File.Exists(boardIdHeader))
        {
            foreach (Match match in BoardVariantIdRegex.Matches(File.ReadAllText(boardIdHeader)))
            {
                string id = match.Groups[1].Value;
                if (seen.Add(id))
                {
                    ids.Add(id);
                }
            }
        }

        foreach (string id in FallbackBoardIds)
        {
            if (seen.Add(id))
            {
                ids.Add(id);
            }
        }

        return ids;
    }

    private static string? GetBoardId(string binText, IReadOnlyList<string> knownBoardIds)
    {
        var markerMatch = BoardMarkerRegex.Match(binText);
        if (markerMatch.Success)
        {
            return markerMatch.Groups[1].Value;
        }

        foreach (string candidate in knownBoardIds)
        {
            if (Regex.IsMatch(binText, $"[^A-Za-z0-9_]{Regex.Escape(candidate)}[^A-Za-z0-9_]"))
            {
                return candidate;
            }
        }
        return null;
    }
}
