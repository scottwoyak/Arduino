<#
.SYNOPSIS
	Publishes the compiled firmware for a single Arduino sketch as a rolling GitHub
	release, so OTAUpdater (see libraries/Woyak/OTAUpdater.h) can find it.

.DESCRIPTION
	This script is meant to be wired into Visual Studio's Tools > External Tools as a
	"Publish Release" command. It:
	  1. Locates the sketch's compiled .ino.bin under its local build output
		  (<SketchName>\build\<board>\<SketchName>.ino.bin), picking the most recently
		  built one if multiple board folders exist.
	  2. Computes the effective compiled version by combining the sketch's own version
		  literal (parsed from its MakeVersion("...") call in the .ino) with the shared
		  LIBRARY_VERSION (libraries\Woyak\LibraryVersion.h), matching the MakeVersion()
		  logic used at compile time.
	  3. Creates (or updates) a single rolling GitHub release tagged with the sketch
		  name, uploading "<SketchName>.<boardId>.ino.bin" and
		  "<SketchName>.<boardId>.version.txt" as assets, overwriting only the assets for
		  the board just built (other boards' assets, if any, are left untouched). The
		  board id matches OTAUpdater::_BOARD_ID (ARDUINO_BOARD_VARIANT_ID, see
		  libraries\Woyak\ArduinoBoard.h) exactly: resolved from a custom variant #define
		  in the sketch's .ino when present (distinguishing physical wiring variants that
		  share the same Arduino IDE board type), otherwise from the sketch's .vcxproj
		  (ARDUINO_BOARD). This allows a single sketch to be published for multiple boards
		  independently, without one board's publish causing another board to redownload
		  an unchanged binary.
	  4. Prints a summary (sketch, version, board, asset names) and prompts for
		  confirmation (y/N) before uploading anything to GitHub.

	Since Visual Studio's External Tools launches this script without an interactive
	console attached, the confirmation prompt above would never receive input. To work
	around that, the script's first (non-interactive) invocation relaunches itself in a
	new, real PowerShell console window (which stays open afterward via -NoExit so you
	can see the result), and the original invocation exits immediately.

	MIGRATING ALREADY-DEPLOYED DEVICES:
	Devices already running the old (pre-board-qualified) OTAUpdater still request the
	unqualified "<SketchName>.ino.bin" / "version.txt" asset names. Once those assets stop
	existing, those devices can no longer discover updates over OTA and would need to be
	physically reflashed. To avoid that, pass -IncludeLegacyAssets on the first publish
	after upgrading OTAUpdater, which additionally uploads the same binary/version under
	the old unqualified names. Old devices will fetch that one last time, install it (it
	contains the new board-qualified OTAUpdater code), and use the new naming convention
	from then on. Once you've confirmed (e.g. via LogServer/Influx) that all deployed
	devices for a sketch have picked up this transition release, stop passing
	-IncludeLegacyAssets on subsequent publishes and optionally delete the legacy assets.

	Requires the GitHub CLI (gh) to be installed and authenticated
	(gh auth login) on the machine running this script.

	ADDING THIS AS A VISUAL STUDIO EXTERNAL TOOL:
	In Visual Studio, go to Tools > External Tools... > Add, then fill in:
	  Title:       Publish Release
	  Command:     powershell.exe
	  Arguments:   -ExecutionPolicy Bypass -File "$(SolutionDir)Tools\Publish-Release.ps1"
	  Initial directory: $(ProjectDir)
	Check "Use Output window" off (the script opens its own console window). With the
	sketch's project selected/active in Solution Explorer, $(ProjectDir) resolves to that
	sketch's folder, which becomes -SketchName's default (via the current directory) and
	-RepoRoot is derived automatically from this script's own location. To always publish
	with -IncludeLegacyAssets, add that flag to the Arguments field as well, or create a
	second External Tools entry (e.g. "Publish Release (with legacy assets)") with
	-IncludeLegacyAssets appended.

.PARAMETER SketchName
	The name of the sketch to publish (e.g. "Temp_Monitor"). Defaults to the name of
	the current directory, so it can be invoked from Visual Studio with
	$(ProjectDir) as the working directory and no explicit argument. If that default
	doesn't resolve to a valid sketch directory (e.g. $(ProjectDir) resolved to the
	solution directory because no document tab from the sketch's project was open),
	the script falls back to asking the running Visual Studio instance for whichever
	project is currently selected/active in Solution Explorer.

.PARAMETER RepoRoot
	Path to the root of the Arduino repo. Defaults to the parent of this script's
	directory (Tools\..).

.PARAMETER IncludeLegacyAssets
	Also uploads the same binary/version under the old unqualified "<SketchName>.ino.bin"
	/ "version.txt" names, so devices still running pre-board-qualified OTAUpdater code
	can find this update. See MIGRATING ALREADY-DEPLOYED DEVICES above.

.EXAMPLE
	.\Publish-Release.ps1 -SketchName Temp_Monitor

.EXAMPLE
	.\Publish-Release.ps1 -SketchName Temp_Monitor -IncludeLegacyAssets
#>

param(
	[string]$SketchName = (Split-Path -Leaf (Get-Location)),
	[string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
	[switch]$IncludeLegacyAssets,
	[switch]$Relaunched,
	[switch]$SketchNameWasDefaulted
)

$ErrorActionPreference = "Stop"

# Captured before the relaunch below (which forwards -SketchName explicitly, since the
# relaunched instance's working directory is $RepoRoot rather than the original one), so
# the active-project fallback further down still knows whether the caller actually
# specified -SketchName or just got the current-directory default.
if (-not $Relaunched)
{
	$SketchNameWasDefaulted = -not $PSBoundParameters.ContainsKey('SketchName')
}

# Visual Studio's Tools > External Tools launches this script without an interactive
# console attached to stdin, so Read-Host (used below to confirm before publishing)
# would silently return an empty string and the script would always cancel. To make
# the confirmation prompt usable, relaunch this same script in a new, real console
# window the first time through (the -Relaunched switch prevents an infinite loop),
# then let the original (non-interactive) invocation exit immediately.
if (-not $Relaunched)
{
	# Deliberately no -NoExit here: the relaunched window should close itself
	# automatically on success. A trap further down catches any terminating error,
	# prints it, and pauses with -NoExit re-added only in that failure path so the
	# window stays open long enough to read the error.
	$argList = @(
		"-ExecutionPolicy", "Bypass",
		"-File", "`"$PSCommandPath`"",
		"-SketchName", "`"$SketchName`"",
		"-RepoRoot", "`"$RepoRoot`"",
		"-Relaunched"
	)
	if ($SketchNameWasDefaulted)
	{
		$argList += "-SketchNameWasDefaulted"
	}
	if ($IncludeLegacyAssets)
	{
		$argList += "-IncludeLegacyAssets"
	}

	Start-Process -FilePath "powershell.exe" -ArgumentList $argList -WorkingDirectory $RepoRoot
	exit 0
}

# Since the relaunch above no longer passes -NoExit, this window will close automatically
# on success. On a terminating error, this trap prints it and pauses (via Read-Host) so
# the window doesn't disappear before the user can read what went wrong, then exits with
# a non-zero code.
trap
{
	Write-Host ""
	Write-Host "ERROR: $_" -ForegroundColor Red
	Read-Host "Press Enter to close this window"
	exit 1
}

# C# helper used by Get-RunningDTE to enumerate the Running Object Table (ROT) and find
# a running Visual Studio DTE automation object, since GetActiveObject("VisualStudio.DTE")
# only works when that exact (unversioned) ProgID happens to be registered - newer/preview
# Visual Studio releases (e.g. 2026) only register a versioned ProgID (e.g.
# "VisualStudio.DTE.18.0"), which a hardcoded ProgID guess would miss entirely. The whole
# enumeration is done in C# (rather than PowerShell calling the COM interop interfaces
# directly) because PowerShell's late-binding cannot invoke methods on the raw
# IRunningObjectTable/IEnumMoniker interface objects (e.g. EnumRunning/GetDisplayName)
# marshaled back from Add-Type. Guarded so re-running in the same session (e.g. via
# -NoExit) doesn't fail with a "type already exists" error.
if (-not ([System.Management.Automation.PSTypeName]'RotHelper').Type)
{
	Add-Type -Language CSharp -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;

public static class RotHelper
{
	public static object GetRunningDTE(out string error)
	{
		error = null;
		IRunningObjectTable rot = null;
		IBindCtx bindCtx = null;
		try
		{
			int hr = NativeMethods.GetRunningObjectTable(0, out rot);
			if (hr != 0 || rot == null)
			{
				error = "GetRunningObjectTable failed (hr=0x" + hr.ToString("X") + ").";
				return null;
			}

			hr = NativeMethods.CreateBindCtx(0, out bindCtx);
			if (hr != 0 || bindCtx == null)
			{
				error = "CreateBindCtx failed (hr=0x" + hr.ToString("X") + ").";
				return null;
			}

			IEnumMoniker monikerEnum = null;
			rot.EnumRunning(out monikerEnum);
			monikerEnum.Reset();

			IMoniker[] fetched = new IMoniker[1];
			int matchCount = 0;
			while (monikerEnum.Next(1, fetched, IntPtr.Zero) == 0)
			{
				IMoniker moniker = fetched[0];
				string displayName = null;
				try
				{
					moniker.GetDisplayName(bindCtx, null, out displayName);
				}
				catch
				{
					continue;
				}

				if (displayName != null && displayName.StartsWith("!VisualStudio.DTE"))
				{
					matchCount++;
					object obj = null;
					try
					{
						rot.GetObject(moniker, out obj);
						if (obj != null)
						{
							return obj;
						}
					}
					catch (Exception ex)
					{
						error = "GetObject failed for '" + displayName + "': " + ex.Message;
					}
				}
			}

			if (matchCount == 0)
			{
				error = "no '!VisualStudio.DTE*' moniker found in the Running Object Table. Is Visual Studio running?";
			}
		}
		catch (Exception ex)
		{
			error = "unexpected error: " + ex.Message;
		}

		return null;
	}

	private static class NativeMethods
	{
		[DllImport("ole32.dll")]
		public static extern int GetRunningObjectTable(int reserved, out IRunningObjectTable pprot);

		[DllImport("ole32.dll")]
		public static extern int CreateBindCtx(int reserved, out IBindCtx ppbc);
	}
}
"@
}

function Get-RunningDTE
{
	# Finds a running Visual Studio DTE automation object via the Running Object Table
	# (see RotHelper.GetRunningDTE above), matching any moniker starting with
	# "!VisualStudio.DTE" (e.g. "!VisualStudio.DTE.18.0:12345"), regardless of the exact
	# VS version. This is more robust than GetActiveObject("VisualStudio.DTE"), which only
	# works for the plain, unversioned ProgID. Any failure is reported via Write-Warning so
	# a silently-missing DTE connection doesn't look identical to "no match found".
	$error = $null
	$dte = [RotHelper]::GetRunningDTE([ref]$error)
	if (-not $dte)
	{
		Write-Warning "Get-RunningDTE: $error"
		return $null
	}

	return $dte
}

function Get-ActiveProjectDirectory
{
	# Visual Studio's $(ProjectDir) External Tools macro is resolved from the active
	# *document* (tab), not the project selected in Solution Explorer, so it comes out
	# empty/wrong whenever no tab from the sketch's project is open, or when the active
	# tab belongs to a non-sketch project (e.g. a library header). As a fallback, ask the
	# running Visual Studio instance directly, via its DTE automation object, for
	# whichever project is currently selected/active in Solution Explorer. If multiple
	# Visual Studio instances are running, an arbitrary one is used, so this is only used
	# as a fallback, not the primary resolution path.
	$dte = Get-RunningDTE
	if (-not $dte)
	{
		return $null
	}

	try
	{
		$selectedProjects = $dte.ActiveSolutionProjects
		if ($selectedProjects)
		{
			foreach ($project in $selectedProjects)
			{
				if ($project -and $project.FullName)
				{
					return (Split-Path -Parent $project.FullName)
				}
			}
		}

		Write-Warning "Get-ActiveProjectDirectory: DTE.ActiveSolutionProjects is empty (no project selected in Solution Explorer)."
	}
	catch
	{
		Write-Warning "Get-ActiveProjectDirectory: failed to read ActiveSolutionProjects: $($_.Exception.Message)"
		return $null
	}
	finally
	{
		[System.Runtime.InteropServices.Marshal]::ReleaseComObject($dte) | Out-Null
	}

	return $null
}

function Get-EffectiveVersion([string]$sketchDir, [string]$sketchName, [string]$repoRoot)
{
	# The sketch's own version is now an inline string literal passed to MakeVersion(...)
	# in the .ino itself, rather than a separate version.txt file, so parse it directly
	# out of the .ino.
	$inoPath = Join-Path $sketchDir "$sketchName.ino"
	if (-not (Test-Path $inoPath))
	{
		throw "Could not find sketch file at '$inoPath'."
	}

	$sketchVersionMatch = Select-String -Path $inoPath -Pattern 'MakeVersion\("([^"]+)"\)' | Select-Object -First 1
	if (-not $sketchVersionMatch)
	{
		throw "Could not parse MakeVersion(...) version literal from '$inoPath'."
	}
	$sketchVersion = $sketchVersionMatch.Matches[0].Groups[1].Value

	$libraryVersionPath = Join-Path $repoRoot "libraries\Woyak\LibraryVersion.h"
	if (-not (Test-Path $libraryVersionPath))
	{
		throw "Could not find LibraryVersion.h at '$libraryVersionPath'."
	}

	$match = Select-String -Path $libraryVersionPath -Pattern 'LIBRARY_VERSION\s*=\s*"([^"]+)"'
	if (-not $match)
	{
		throw "Could not parse LIBRARY_VERSION from '$libraryVersionPath'."
	}
	$libraryVersion = $match.Matches[0].Groups[1].Value

	return "$sketchVersion.$libraryVersion"
}

function Find-LatestBin([string]$sketchDir, [string]$sketchName)
{
	$buildsRoot = Join-Path $sketchDir "build"
	if (-not (Test-Path $buildsRoot))
	{
		throw "No local build output found for '$sketchName' under '$buildsRoot'. Build the sketch in Visual Studio first."
	}

	$binName = "$sketchName.ino.bin"
	# Exclude build\_publish, which is this script's own staging output (see below); without
	# this, a previously staged/legacy-named binary left there from an earlier publish could
	# be picked up as the "latest" build, causing later Copy-Item calls to copy a file onto itself.
	$candidates = Get-ChildItem -Path $buildsRoot -Recurse -Filter $binName -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -notlike (Join-Path $buildsRoot "_publish\*") }
	if (-not $candidates -or $candidates.Count -eq 0)
	{
		throw "No '$binName' found under '$buildsRoot'. Build the sketch in Visual Studio first."
	}

	return ($candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1)
}

function Get-BoardIdFromBinary([string]$binPath)
{
	# Scans the compiled .bin for the ARDUINO_BOARD_VARIANT_ID string literal (see
	# OTAUpdater::_BOARD_ID in libraries\Woyak\OTAUpdater.h), which the compiler embeds
	# as a plain null-terminated ASCII string. Reads the file as raw bytes (rather than
	# text) since a firmware binary isn't valid text and would otherwise be corrupted/
	# misinterpreted by encoding conversion.
	$bytes = [System.IO.File]::ReadAllBytes($binPath)
	$text = [System.Text.Encoding]::ASCII.GetString($bytes)

	# All known ARDUINO_BOARD_VARIANT_ID values (see libraries\Woyak\ArduinoBoard.h); the
	# string must match one of these exactly, framed by non-identifier bytes, to avoid
	# false positives from incidental substrings elsewhere in the binary.
	$knownBoardIds = @(
		'ADAFRUIT_FEATHER_M0',
		'ADAFRUIT_FEATHER_ESP32S3_TFT',
		'WAVESHARE_ESP32_S3_ZERO_SENSORS',
		'WAVESHARE_ESP32_S3_ZERO',
		'WAVESHARE_ESP32S3_TOUCH_LCD_43',
		'HOSYOND_ESP32_S3_VIEWER',
		'ESP32S3_DEV_PLAYGROUND'
	)

	foreach ($candidate in $knownBoardIds)
	{
		if ($text -match "[^A-Za-z0-9_]$candidate[^A-Za-z0-9_]")
		{
			return $candidate
		}
	}

	return $null
}

function Get-BoardId([string]$sketchDir, [string]$sketchName, [string]$binPath)
{

	# string literal ARDUINO_BOARD_VARIANT_ID (defined per-branch in
	# libraries\Woyak\ArduinoBoard.h), so it's embedded verbatim in the compiled .bin.
	# Reading it directly from the binary is the authoritative source - unlike inferring
	# it from the .vcxproj/.ino source below, it can never drift out of sync with what
	# was actually compiled (e.g. a stale variant #define left in the .ino, or a build
	# that's older than the current source).
	if ($binPath -and (Test-Path $binPath))
	{
		$boardIdFromBinary = Get-BoardIdFromBinary -binPath $binPath
		if ($boardIdFromBinary)
		{
			return $boardIdFromBinary
		}

		Write-Warning "Get-BoardId: could not find ARDUINO_BOARD_VARIANT_ID in '$binPath'; falling back to .vcxproj/.ino inference."
	}

	# Match OTAUpdater::_BOARD_ID (libraries\Woyak\OTAUpdater.h), which now resolves to
	# ARDUINO_BOARD_VARIANT_ID (defined per-branch in libraries\Woyak\ArduinoBoard.h). That
	# macro distinguishes physical wiring variants that share the same underlying Arduino
	# IDE board type (e.g. Hosyond Viewer vs. generic Playground both build as
	# ARDUINO_BOARD=ESP32S3_DEV), so we must resolve it the same way here: read the raw
	# ARDUINO_BOARD macro from the .vcxproj first (reflecting whichever board is actually
	# selected/built in the IDE), and only consult the sketch's .ino for a custom variant
	# #define when that board type is ambiguous (i.e. ESP32S3_DEV, which multiple wiring
	# variants share). Checking the .ino unconditionally would misidentify the board
	# whenever a different board type (e.g. an Adafruit Feather) is currently selected but
	# the sketch still contains a variant #define from a previous board.
	$vcxprojPath = Join-Path $sketchDir "$sketchName.vcxproj"
	if (-not (Test-Path $vcxprojPath))
	{
		throw "Could not find project file at '$vcxprojPath' to determine the board."
	}

	$match = Select-String -Path $vcxprojPath -Pattern 'ARDUINO_BOARD=([A-Za-z0-9_]+)' | Select-Object -First 1
	if (-not $match)
	{
		throw "Could not find ARDUINO_BOARD in '$vcxprojPath'."
	}

	$boardId = $match.Matches[0].Groups[1].Value
	if ($boardId -ne 'ESP32S3_DEV' -and $boardId -ne 'WAVESHARE_ESP32_S3_ZERO')
	{
		return $boardId
	}

	# ESP32S3_DEV and WAVESHARE_ESP32_S3_ZERO are both ambiguous: the raw ARDUINO_BOARD
	# macro reflects only the underlying Arduino core board type, which is shared by
	# multiple physical wiring variants (e.g. WAVESHARE_ESP32_S3_ZERO vs. its
	# WAVESHARE_ESP32_S3_ZERO_SENSORS counterpart with a custom-powered I2C bus and RGB
	# LED). Disambiguate via the sketch's .ino variant #define, if present.
	$inoPath = Join-Path $sketchDir "$sketchName.ino"
	if (Test-Path $inoPath)
	{
		$variantMatch = Select-String -Path $inoPath -Pattern '^\s*#define\s+ARDUINO_(HOSYOND_ESP32_S3_VIEWER|WAVESHARE_ESP32_S3_ZERO_SENSORS)\b' | Select-Object -First 1
		if ($variantMatch)
		{
			return $variantMatch.Matches[0].Groups[1].Value
		}
	}

	# Generic ESP32S3 Dev Module boards with no variant #define are assumed to be wired up
	# as a Playground setup (see ArduinoBoard.h), matching ARDUINO_BOARD_VARIANT_ID's
	# "_PLAYGROUND" suffix. WAVESHARE_ESP32_S3_ZERO with no variant #define matches
	# ArduinoBoard.h's own plain WAVESHARE_ESP32_S3_ZERO branch, so it's returned as-is.
	if ($boardId -eq 'ESP32S3_DEV')
	{
		return 'ESP32S3_DEV_PLAYGROUND'
	}

	return $boardId
}

$sketchDir = Join-Path $RepoRoot $SketchName
if ($SketchNameWasDefaulted -and (-not (Test-Path (Join-Path $sketchDir "$SketchName.ino"))))
{
	# The default (current-directory-derived) SketchName isn't a valid sketch, most
	# likely because $(ProjectDir) didn't resolve to the intended sketch (e.g. no
	# document tab from that project was open). Fall back to whichever project is
	# actually selected/active in Visual Studio's Solution Explorer.
	$activeProjectDir = Get-ActiveProjectDirectory
	if ($activeProjectDir -and (Test-Path (Join-Path $activeProjectDir "$(Split-Path -Leaf $activeProjectDir).ino")))
	{
		$SketchName = Split-Path -Leaf $activeProjectDir
		$sketchDir = $activeProjectDir
		Write-Host "Resolved sketch from Visual Studio's active project: '$SketchName'."
	}
}

Write-Host "Publishing release for sketch '$SketchName'..."

if (-not (Test-Path $sketchDir))
{
	throw "Sketch directory not found: '$sketchDir'."
}

$version = Get-EffectiveVersion -sketchDir $sketchDir -sketchName $SketchName -repoRoot $RepoRoot
Write-Host "Effective version: $version"

$binFile = Find-LatestBin -sketchDir $sketchDir -sketchName $SketchName
Write-Host "Using firmware: $($binFile.FullName) (built $($binFile.LastWriteTime))"

$boardId = Get-BoardId -sketchDir $sketchDir -sketchName $SketchName -binPath $binFile.FullName
Write-Host "Board: $boardId"

# Stage version.txt (board-qualified) with the *effective* compiled version, not the raw
# sketch version, so OTAUpdater's version check compares against what was actually
# compiled in. Board-qualifying this avoids one board's publish making another board
# (whose binary wasn't republished) think an update is available.
$stagingDir = Join-Path $sketchDir "build\_publish"
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null
$stagedVersionName = "$SketchName.$boardId.version.txt"
$stagedVersionPath = Join-Path $stagingDir $stagedVersionName
Set-Content -Path $stagedVersionPath -Value "`"$version`"" -NoNewline

# Stage the firmware under a board-qualified name (matching OTAUpdater::_deriveUrls(),
# which builds "{sketchName}.{boardId}.ino.bin") so multiple boards' binaries can coexist
# as separate assets within the same rolling release.
$stagedBinName = "$SketchName.$boardId.ino.bin"
$stagedBinPath = Join-Path $stagingDir $stagedBinName
Copy-Item -Path $binFile.FullName -Destination $stagedBinPath -Force

$assetPaths = @($stagedBinPath, $stagedVersionPath)

if ($IncludeLegacyAssets)
{
	# Also publish under the old unqualified names so devices still running
	# pre-board-qualified OTAUpdater code can find this transition update (see
	# MIGRATING ALREADY-DEPLOYED DEVICES in the script header).
	$legacyBinPath = Join-Path $stagingDir "$SketchName.ino.bin"
	$legacyVersionPath = Join-Path $stagingDir "version.txt"
	Copy-Item -Path $binFile.FullName -Destination $legacyBinPath -Force
	Set-Content -Path $legacyVersionPath -Value "`"$version`"" -NoNewline
	$assetPaths += $legacyBinPath, $legacyVersionPath
	Write-Host "Including legacy unqualified assets for migration."
}

$tag = $SketchName

Write-Host ""
Write-Host "About to publish the following to release '$tag' (https://github.com/scottwoyak/Arduino/releases/tag/$tag):"
Write-Host "  Sketch:  $SketchName"
Write-Host "  Version: $version"
Write-Host "  Board:   $boardId"
Write-Host "  Assets:"
foreach ($assetPath in $assetPaths)
{
	Write-Host "    - $(Split-Path -Leaf $assetPath)"
}
Write-Host ""
$confirmation = Read-Host "Proceed with publish? (y/N)"
if ($confirmation -notin @('y', 'Y', 'yes', 'Yes'))
{
	Write-Host "Publish cancelled."
	exit 0
}

$releaseExists = $true

# gh release view exits non-zero when the release doesn't exist yet (e.g. the first
# publish of a new sketch). With $ErrorActionPreference = "Stop" (set at the top of this
# script) and PowerShell 7's $PSNativeCommandUseErrorActionPreference (on by default),
# that non-zero exit/stderr output is turned into a terminating error before
# $LASTEXITCODE can be checked below, regardless of the *> $null redirection (which only
# affects the literal output streams, not the synthesized error record). Both preferences
# must be relaxed around this call so a missing release is treated as normal, expected
# control flow instead of a script-ending error. $PSNativeCommandUseErrorActionPreference
# doesn't exist on Windows PowerShell 5.1, so guard its use.
$hasNativeCommandErrorPref = Test-Path variable:PSNativeCommandUseErrorActionPreference
if ($hasNativeCommandErrorPref)
{
	$previousNativeCommandErrorActionPreference = $PSNativeCommandUseErrorActionPreference
	$PSNativeCommandUseErrorActionPreference = $false
}

$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"

try
{
	gh release view $tag --repo scottwoyak/Arduino *> $null
	if ($LASTEXITCODE -ne 0)
	{
		$releaseExists = $false
	}
}
finally
{
	$ErrorActionPreference = $previousErrorActionPreference
	if ($hasNativeCommandErrorPref)
	{
		$PSNativeCommandUseErrorActionPreference = $previousNativeCommandErrorActionPreference
	}
}

if ($releaseExists)
{
	Write-Host "Updating existing release '$tag'..."
	gh release upload $tag @assetPaths --repo scottwoyak/Arduino --clobber
	gh release edit $tag --repo scottwoyak/Arduino --title "$SketchName $version" --notes "Automated publish of $SketchName v$version"
}
else
{
	Write-Host "Creating new release '$tag'..."
	gh release create $tag @assetPaths --repo scottwoyak/Arduino --title "$SketchName $version" --notes "Automated publish of $SketchName v$version"
}

if ($LASTEXITCODE -ne 0)
{
	throw "gh release command failed with exit code $LASTEXITCODE."
}

Write-Host "Published '$SketchName' v$version to release '$tag'."
