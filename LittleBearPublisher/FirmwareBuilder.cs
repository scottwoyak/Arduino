using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace LittleBearPublisher;

/// <summary>
/// Compiles Arduino sketches headlessly via arduino-cli for configured board targets.
/// </summary>
internal sealed class FirmwareBuilder
{
    internal sealed record BoardConfig(string BoardId, string DisplayName, string BuildFolder, string DefaultFqbn);

    private static readonly Regex BuildBoardLineRegex = new(@"^([A-Za-z0-9_]+)\.build\.board\s*=\s*([A-Za-z0-9_]+)\s*$", RegexOptions.Multiline);
    private static readonly Regex BoardNameLineRegex = new(@"^([A-Za-z0-9_]+)\.name\s*=\s*(.+?)\s*$", RegexOptions.Multiline);
    private static readonly Regex ConditionalBoardIdRegex = new(@"defined\s*\(\s*(?:ARDUINO_)?([A-Za-z0-9_]+)\s*\)[\s\S]*?#define\s+ARDUINO_BOARD_VARIANT_ID\s+""([^""]+)""");
    private static readonly Regex VcxprojBoardMacroRegex = new(@"ARDUINO_BOARD=([A-Za-z0-9_]+)");

    private static readonly Dictionary<string, BoardConfig> DefaultBoardConfigs = new(StringComparer.OrdinalIgnoreCase)
    {
        ["ADAFRUIT_FEATHER_M0"] = new(
            "ADAFRUIT_FEATHER_M0",
            "Adafruit Feather M0",
            "adafruit.samd.adafruit_feather_m0",
            "adafruit:samd:adafruit_feather_m0"),

        ["ADAFRUIT_FEATHER_ESP32S3_TFT"] = new(
            "ADAFRUIT_FEATHER_ESP32S3_TFT",
            "Adafruit Feather ESP32-S3 TFT",
            "esp32.esp32.adafruit_feather_esp32s3_tft",
            "esp32:esp32:adafruit_feather_esp32s3_tft:UploadSpeed=921600,USBMode=default,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=cdc,CPUFreq=240,FlashMode=qio,FlashSize=4M,DebugLevel=none,PSRAM=enabled,LoopCore=1,EventsCore=1,ZigbeeMode=default"),

        ["WAVESHARE_ESP32S3_ZERO_SENSORS"] = new(
            "WAVESHARE_ESP32S3_ZERO_SENSORS",
            "Waveshare ESP32-S3-Zero (Sensors)",
            "esp32.esp32.waveshare_esp32_s3_zero_sensors",
            "esp32:esp32:waveshare_esp32_s3_zero_sensors:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=default,PartitionScheme=min_spiffs,PSRAM=enabled"),

        ["WAVESHARE_ESP32S3_ZERO"] = new(
            "WAVESHARE_ESP32S3_ZERO",
            "Waveshare ESP32-S3-Zero",
            "esp32.esp32.waveshare_esp32_s3_zero",
            "esp32:esp32:waveshare_esp32_s3_zero:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,PartitionScheme=min_spiffs,DebugLevel=none,PSRAM=enabled,LoopCore=1,EventsCore=1"),

        ["HOSYOND_ESP32S3_TOUCH_LCD_40"] = new(
            "HOSYOND_ESP32S3_TOUCH_LCD_40",
            "Hosyond 4.0 ESP32-S3-Touch-LCD",
            "esp32.esp32.hosyond_esp32_s3_touch_lcd_40",
            "esp32:esp32:hosyond_esp32_s3_touch_lcd_40:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi"),

        ["WAVESHARE_ESP32S3_TOUCH_LCD_19"] = new(
            "WAVESHARE_ESP32S3_TOUCH_LCD_19",
            "Waveshare 1.9 ESP32-S3-Touch-LCD",
            "esp32.esp32.waveshare_esp32_s3_touch_lcd_19",
            "esp32:esp32:waveshare_esp32_s3_touch_lcd_19:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=default,PSRAM=opi"),

        ["WAVESHARE_ESP32S3_TOUCH_LCD_28"] = new(
            "WAVESHARE_ESP32S3_TOUCH_LCD_28",
            "Waveshare 2.8 ESP32-S3-Touch-LCD",
            "esp32.esp32.waveshare_esp32_s3_touch_lcd_28",
            "esp32:esp32:waveshare_esp32_s3_touch_lcd_28:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=default,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled"),

        ["WAVESHARE_ESP32S3_TOUCH_LCD_43"] = new(
            "WAVESHARE_ESP32S3_TOUCH_LCD_43",
            "Waveshare ESP32-S3-Touch-LCD-4.3",
            "esp32.esp32.esp32s3",
            "esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=min_spiffs,DebugLevel=none,PSRAM=enabled,LoopCore=1,EventsCore=1,JTAGAdapter=default,ZigbeeMode=default"),

        ["ESP32S3_DEV_PLAYGROUND"] = new(
            "ESP32S3_DEV_PLAYGROUND",
            "ESP32-S3 Playground",
            "esp32.esp32.esp32s3_dev_playground",
            "esp32:esp32:esp32s3_dev_playground:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi"),
    };

    /// <summary>
    /// Explicit mapping of OTA-enabled sketches to the boards they support.
    /// </summary>
    private static readonly Dictionary<string, string[]> SketchBoards = new(StringComparer.OrdinalIgnoreCase)
    {
        ["Air_Monitor_Display"] = ["ADAFRUIT_FEATHER_ESP32S3_TFT"],
        ["Air_Viewer"] = ["HOSYOND_ESP32S3_TOUCH_LCD_40"],
        ["Gate_Opener"] = ["WAVESHARE_ESP32S3_ZERO_SENSORS"],
        ["Gate_Publisher"] = ["WAVESHARE_ESP32S3_ZERO_SENSORS"],
        ["Gate_Viewer"] = ["ADAFRUIT_FEATHER_ESP32S3_TFT", "HOSYOND_ESP32S3_TOUCH_LCD_40"],
        ["Lake_Temp_Monitor"] = ["WAVESHARE_ESP32S3_ZERO_SENSORS"],
        ["Light_Monitor"] = ["WAVESHARE_ESP32S3_ZERO_SENSORS"],
        ["Muti_Temp_Playground"] = ["ESP32S3_DEV_PLAYGROUND"],
        ["Telemetry_Publisher_Display"] = ["ADAFRUIT_FEATHER_ESP32S3_TFT"],
        ["Telemetry_Publisher_Playground"] = ["ESP32S3_DEV_PLAYGROUND"],
        ["Telemetry_Subscriber_Display"] = ["ADAFRUIT_FEATHER_ESP32S3_TFT"],
        ["Temp_Monitor"] = ["WAVESHARE_ESP32S3_ZERO_SENSORS"],
        ["Temp_Monitor_Display"] = ["ADAFRUIT_FEATHER_ESP32S3_TFT", "HOSYOND_ESP32S3_TOUCH_LCD_40"],
        ["Wave_Publisher"] = ["WAVESHARE_ESP32S3_ZERO_SENSORS"],
        ["Wave_Viewer"] = ["HOSYOND_ESP32S3_TOUCH_LCD_40"],
        ["Wind_Publisher"] = ["WAVESHARE_ESP32S3_ZERO_SENSORS"],
        ["Wind_Viewer"] = ["ADAFRUIT_FEATHER_ESP32S3_TFT", "HOSYOND_ESP32S3_TOUCH_LCD_40"],
    };

    private readonly string _repoRoot;

    public FirmwareBuilder(string repoRoot)
    {
        _repoRoot = repoRoot;
    }

    /// <summary>
    /// Returns the configured board IDs for <paramref name="sketch"/>. Discovers boards from
    /// existing build subdirectories (build.options.json) first, then merges with the explicit
    /// sketch mapping or category fallback.
    /// </summary>
    public static IReadOnlyList<string> GetSupportedBoards(string repoRoot, string sketch, string? inoText = null)
    {
        Dictionary<string, BoardConfig> configs = LoadBoardConfigs(repoRoot);
        var discovered = new List<string>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        string buildsRoot = Path.Combine(repoRoot, sketch, "build");
        if (Directory.Exists(buildsRoot))
        {
            foreach (string subDir in Directory.EnumerateDirectories(buildsRoot))
            {
                string folderName = Path.GetFileName(subDir);
                if (string.Equals(folderName, "_publish", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                BoardConfig? match = configs.Values.FirstOrDefault(c => string.Equals(c.BuildFolder, folderName, StringComparison.OrdinalIgnoreCase));
                if (match != null && seen.Add(match.BoardId))
                {
                    discovered.Add(match.BoardId);
                }
            }
        }

        string vcxprojPath = Path.Combine(repoRoot, sketch, sketch + ".vcxproj");
        if (File.Exists(vcxprojPath))
        {
            try
            {
                Dictionary<string, string> macroToVariantId = LoadMacroToVariantId(repoRoot);
                foreach (Match m in VcxprojBoardMacroRegex.Matches(File.ReadAllText(vcxprojPath)))
                {
                    string token = m.Groups[1].Value;
                    string variantId = macroToVariantId.TryGetValue(token, out string? mapped) ? mapped : token;
                    if (configs.ContainsKey(variantId) && seen.Add(variantId))
                    {
                        discovered.Add(variantId);
                    }
                }
            }
            catch (IOException)
            {
                // Ignore if locked while Visual Studio is saving.
            }
        }

        IReadOnlyList<string> fallback = GetDefaultSketchBoards(sketch, inoText);
        foreach (string board in fallback)
        {
            if (seen.Add(board))
            {
                discovered.Add(board);
            }
        }

        return discovered;
    }

    private static IReadOnlyList<string> GetDefaultSketchBoards(string sketch, string? inoText)
    {
        if (SketchBoards.TryGetValue(sketch, out string[]? boards))
        {
            return boards;
        }

        if (inoText != null &&
            (inoText.Contains("ARDUINO_WAVESHARE_ESP32S3_ZERO_SENSORS", StringComparison.Ordinal) ||
             inoText.Contains("ARDUINO_WAVESHARE_ESP32_S3_ZERO_SENSORS", StringComparison.Ordinal)))
        {
            return ["WAVESHARE_ESP32S3_ZERO_SENSORS"];
        }

        if (sketch.EndsWith("_Playground", StringComparison.OrdinalIgnoreCase))
        {
            return ["ESP32S3_DEV_PLAYGROUND"];
        }

        if (sketch.EndsWith("_Viewer", StringComparison.OrdinalIgnoreCase))
        {
            return ["ADAFRUIT_FEATHER_ESP32S3_TFT", "HOSYOND_ESP32S3_TOUCH_LCD_40"];
        }

        if (sketch.EndsWith("_Display", StringComparison.OrdinalIgnoreCase))
        {
            return ["ADAFRUIT_FEATHER_ESP32S3_TFT"];
        }

        return ["WAVESHARE_ESP32S3_ZERO"];
    }

    private static Dictionary<string, string> LoadMacroToVariantId(string repoRoot)
    {
        var macroToVariantId = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        string boardIdHeader = Path.Combine(repoRoot, "libraries", "Woyak", "ArduinoBoardId.h");
        if (File.Exists(boardIdHeader))
        {
            foreach (Match m in ConditionalBoardIdRegex.Matches(File.ReadAllText(boardIdHeader)))
            {
                macroToVariantId[m.Groups[1].Value] = m.Groups[2].Value;
            }
        }

        return macroToVariantId;
    }

    /// <summary>
    /// Returns the human-readable board name (matching Visual Studio's board selector) for <paramref name="boardId"/>.
    /// </summary>
    public static string GetBoardDisplayName(string repoRoot, string boardId)
    {
        Dictionary<string, BoardConfig> configs = LoadBoardConfigs(repoRoot);
        return configs.TryGetValue(boardId, out BoardConfig? config) && !string.IsNullOrWhiteSpace(config.DisplayName)
            ? config.DisplayName
            : boardId;
    }

    private static Dictionary<string, BoardConfig> LoadBoardConfigs(string repoRoot)
    {
        var configs = new Dictionary<string, BoardConfig>(DefaultBoardConfigs, StringComparer.OrdinalIgnoreCase);

        // Map build.board macro token (e.g. HOSYOND_ESP32S3_TOUCH_LCD_40) to ARDUINO_BOARD_VARIANT_ID
        // from ArduinoBoardId.h when they differ, or use the build.board token directly.
        Dictionary<string, string> macroToVariantId = LoadMacroToVariantId(repoRoot);

        string boardsLocalPath = Path.Combine(repoRoot, "boards.local.txt");
        if (File.Exists(boardsLocalPath))
        {
            string text = File.ReadAllText(boardsLocalPath);
            var namesByBoardKey = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (Match m in BoardNameLineRegex.Matches(text))
            {
                namesByBoardKey[m.Groups[1].Value] = m.Groups[2].Value.Trim();
            }

            foreach (Match m in BuildBoardLineRegex.Matches(text))
            {
                string boardKey = m.Groups[1].Value;
                string buildBoardToken = m.Groups[2].Value;
                string variantId = macroToVariantId.TryGetValue(buildBoardToken, out string? mapped)
                    ? mapped
                    : buildBoardToken;

                string buildFolder = $"esp32.esp32.{boardKey}";
                configs.TryGetValue(variantId, out BoardConfig? existing);
                string displayName = namesByBoardKey.TryGetValue(boardKey, out string? parsedName)
                    ? parsedName
                    : existing?.DisplayName ?? variantId;
                string defaultFqbn = existing != null &&
                                     existing.DefaultFqbn.StartsWith($"esp32:esp32:{boardKey}:", StringComparison.OrdinalIgnoreCase)
                    ? existing.DefaultFqbn
                    : $"esp32:esp32:{boardKey}";

                configs[variantId] = new BoardConfig(variantId, displayName, buildFolder, defaultFqbn);
            }
        }

        return configs;
    }

    /// <summary>
    /// Compiles one (sketch, boardId) target using arduino-cli.
    /// </summary>
    public async Task<(bool Ok, string Detail)> BuildAsync(
        string sketch,
        string boardId,
        IProgress<string>? progress = null,
        CancellationToken cancellationToken = default)
    {
        Dictionary<string, BoardConfig> configs = LoadBoardConfigs(_repoRoot);
        string sketchDir = Path.Combine(_repoRoot, sketch);
        if (!configs.TryGetValue(boardId, out BoardConfig? config))
        {
            config = FindConfigFromExistingBuilds(sketchDir, boardId);
            if (config == null)
            {
                return (false, $"No board configuration defined for '{boardId}'.");
            }
        }

        string? cliPath = FindArduinoCli();
        if (cliPath == null)
        {
            return (false, "Could not locate arduino-cli.exe.");
        }

        string inoPath = Path.Combine(sketchDir, sketch + ".ino");
        if (!File.Exists(inoPath))
        {
            return (false, $"Sketch file not found: {inoPath}");
        }

        string buildDir = ResolveBuildDirectory(sketchDir, config);
        string fqbn = ResolveFqbn(buildDir, config);
        string librariesDir = Path.Combine(_repoRoot, "libraries");

        Directory.CreateDirectory(buildDir);

        // Touch the .ino file if LibraryVersion.h is newer or if we are rebuilding so that
        // __DATE__ / __TIME__ (BUILD@) and MakeVersion (VERSION@) in the .ino translation unit
        // are always recompiled into the output binary.
        try
        {
            File.SetLastWriteTimeUtc(inoPath, DateTime.UtcNow);
        }
        catch (IOException)
        {
            // Ignore if locked; arduino-cli will still attempt compilation.
        }

        var psi = new ProcessStartInfo
        {
            FileName = cliPath,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WorkingDirectory = sketchDir,
        };

        psi.ArgumentList.Add("compile");
        psi.ArgumentList.Add("--fqbn");
        psi.ArgumentList.Add(fqbn);
        psi.ArgumentList.Add("--build-path");
        psi.ArgumentList.Add(buildDir);
        psi.ArgumentList.Add("--libraries");
        psi.ArgumentList.Add(librariesDir);
        psi.ArgumentList.Add("-v");
        psi.ArgumentList.Add(sketchDir);

        try
        {
            using var process = new Process { StartInfo = psi };
            var stdout = new StringBuilder();
            var stderr = new StringBuilder();
            string currentStep = "Starting...";

            void HandleLine(string? line, StringBuilder target)
            {
                if (line == null)
                {
                    return;
                }

                lock (target)
                {
                    target.AppendLine(line);
                }

                string? step = SummarizeVerboseLine(line);
                if (step != null && !string.Equals(step, currentStep, StringComparison.Ordinal))
                {
                    currentStep = step;
                    progress?.Report(step);
                }
            }

            process.OutputDataReceived += (_, e) => HandleLine(e.Data, stdout);
            process.ErrorDataReceived += (_, e) => HandleLine(e.Data, stderr);

            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();

            try
            {
                await process.WaitForExitAsync(cancellationToken);
            }
            catch (OperationCanceledException)
            {
                TryKillProcessTree(process);
                throw;
            }

            if (process.ExitCode == 0)
            {
                return (true, "Built successfully");
            }

            string errorText;
            lock (stderr)
            {
                errorText = stderr.ToString().Trim();
            }
            if (errorText.Length == 0)
            {
                lock (stdout)
                {
                    errorText = stdout.ToString().Trim();
                }
            }

            string firstErrorLine = errorText
                .Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries)
                .FirstOrDefault(l => l.Contains("error:", StringComparison.OrdinalIgnoreCase))
                ?? errorText.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries).LastOrDefault()
                ?? $"exit code {process.ExitCode}";

            return (false, firstErrorLine);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            return (false, ex.Message);
        }
    }

    private static void TryKillProcessTree(Process process)
    {
        try
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
            }
        }
        catch
        {
            // Ignore if process already exited or cannot be killed.
        }
    }

    private static readonly Regex SourceFileTokenRegex = new(@"""?([^""\s]+\.(?:ino\.cpp|cpp|c|S))""?(?:\s|$)", RegexOptions.IgnoreCase);

    private static string? SummarizeVerboseLine(string line)
    {
        string trimmed = line.Trim();
        if (trimmed.Length == 0)
        {
            return null;
        }

        if (trimmed.StartsWith("Detecting libraries used", StringComparison.OrdinalIgnoreCase))
        {
            return "Detecting libraries...";
        }
        if (trimmed.StartsWith("Generating function prototypes", StringComparison.OrdinalIgnoreCase))
        {
            return "Generating prototypes...";
        }
        if (trimmed.StartsWith("Compiling sketch", StringComparison.OrdinalIgnoreCase))
        {
            return "Compiling sketch...";
        }
        if (trimmed.StartsWith("Compiling libraries", StringComparison.OrdinalIgnoreCase))
        {
            return "Compiling libraries...";
        }
        if (trimmed.StartsWith("Compiling library \"", StringComparison.OrdinalIgnoreCase))
        {
            int start = "Compiling library \"".Length;
            int end = trimmed.IndexOf('"', start);
            if (end > start)
            {
                return $"Lib: {trimmed[start..end]}";
            }
        }
        if (trimmed.StartsWith("Compiling core", StringComparison.OrdinalIgnoreCase))
        {
            return "Compiling core...";
        }
        if (trimmed.StartsWith("Archiving built core", StringComparison.OrdinalIgnoreCase))
        {
            return "Archiving core...";
        }
        if (trimmed.StartsWith("Linking everything together", StringComparison.OrdinalIgnoreCase))
        {
            return "Linking...";
        }
        if (trimmed.Contains("esptool", StringComparison.OrdinalIgnoreCase) &&
            trimmed.Contains("elf2image", StringComparison.OrdinalIgnoreCase))
        {
            return "Creating binary image...";
        }

        if ((trimmed.Contains("-g++", StringComparison.OrdinalIgnoreCase) ||
             trimmed.Contains("-gcc", StringComparison.OrdinalIgnoreCase)) &&
            trimmed.Contains(" -c ", StringComparison.Ordinal) &&
            !trimmed.Contains("-E -x", StringComparison.Ordinal))
        {
            MatchCollection matches = SourceFileTokenRegex.Matches(trimmed);
            if (matches.Count > 0)
            {
                string path = matches[^1].Groups[1].Value;
                string fileName = Path.GetFileName(path);
                if (fileName.EndsWith(".ino.cpp", StringComparison.OrdinalIgnoreCase))
                {
                    fileName = fileName[..^4];
                }
                return $"Compiling {fileName}";
            }
        }

        return null;
    }

    private static BoardConfig? FindConfigFromExistingBuilds(string sketchDir, string boardId)
    {
        string buildsRoot = Path.Combine(sketchDir, "build");
        if (!Directory.Exists(buildsRoot))
        {
            return null;
        }

        string sketchName = Path.GetFileName(sketchDir);
        foreach (string subDir in Directory.EnumerateDirectories(buildsRoot))
        {
            string folderName = Path.GetFileName(subDir);
            if (string.Equals(folderName, "_publish", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            string binPath = Path.Combine(subDir, sketchName + ".ino.bin");
            string optionsPath = Path.Combine(subDir, "build.options.json");
            if (!File.Exists(binPath) || !File.Exists(optionsPath))
            {
                continue;
            }

            try
            {
                string binText = Encoding.Latin1.GetString(File.ReadAllBytes(binPath));
                if (!binText.Contains($"BOARD@{boardId}", StringComparison.Ordinal) &&
                    !Regex.IsMatch(binText, $"[^A-Za-z0-9_]{Regex.Escape(boardId)}[^A-Za-z0-9_]"))
                {
                    continue;
                }

                using var doc = JsonDocument.Parse(File.ReadAllText(optionsPath));
                if (doc.RootElement.TryGetProperty("fqbn", out JsonElement fqbnProp))
                {
                    string? fqbn = fqbnProp.GetString();
                    if (!string.IsNullOrWhiteSpace(fqbn))
                    {
                        return new BoardConfig(boardId, boardId, folderName, fqbn);
                    }
                }
            }
            catch
            {
                // Ignore unreadable build folders.
            }
        }

        return null;
    }

    private static string ResolveBuildDirectory(string sketchDir, BoardConfig config)
    {
        string preferredDir = Path.Combine(sketchDir, "build", config.BuildFolder);
        if (Directory.Exists(preferredDir))
        {
            return preferredDir;
        }

        // If the sketch already has a build folder whose binary was compiled for this boardId
        // (e.g. before a board key rename in boards.local.txt), reuse that folder if preferredDir
        // does not exist yet.
        BoardConfig? existing = FindConfigFromExistingBuilds(sketchDir, config.BoardId);
        if (existing != null)
        {
            return Path.Combine(sketchDir, "build", existing.BuildFolder);
        }

        return preferredDir;
    }

    private static string ResolveFqbn(string buildDir, BoardConfig config)
    {
        string optionsPath = Path.Combine(buildDir, "build.options.json");
        if (File.Exists(optionsPath))
        {
            try
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(optionsPath));
                if (doc.RootElement.TryGetProperty("fqbn", out JsonElement fqbnProp))
                {
                    string? existingFqbn = fqbnProp.GetString();
                    if (!string.IsNullOrWhiteSpace(existingFqbn))
                    {
                        return existingFqbn;
                    }
                }
            }
            catch
            {
                // Fall back to default FQBN if build.options.json cannot be parsed.
            }
        }

        return config.DefaultFqbn;
    }

    private static string? FindArduinoCli()
    {
        string arduinoIdeBundled = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
            "Arduino IDE",
            "resources",
            "app",
            "lib",
            "backend",
            "resources",
            "arduino-cli.exe");

        if (File.Exists(arduinoIdeBundled))
        {
            return arduinoIdeBundled;
        }

        string? pathEnv = Environment.GetEnvironmentVariable("PATH");
        if (!string.IsNullOrEmpty(pathEnv))
        {
            foreach (string dir in pathEnv.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
            {
                string candidate = Path.Combine(dir, "arduino-cli.exe");
                if (File.Exists(candidate))
                {
                    return candidate;
                }
            }
        }

        return null;
    }
}
