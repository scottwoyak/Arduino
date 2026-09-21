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
	  2. Computes the effective compiled version by combining the sketch's own
		  version.txt with the shared LIBRARY_VERSION (libraries\Woyak\LibraryVersion.h),
		  matching the MakeVersion() logic used at compile time.
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
	$(ProjectDir) as the working directory and no explicit argument.

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
	[switch]$Relaunched
)

$ErrorActionPreference = "Stop"

# Visual Studio's Tools > External Tools launches this script without an interactive
# console attached to stdin, so Read-Host (used below to confirm before publishing)
# would silently return an empty string and the script would always cancel. To make
# the confirmation prompt usable, relaunch this same script in a new, real console
# window the first time through (the -Relaunched switch prevents an infinite loop),
# then let the original (non-interactive) invocation exit immediately.
if (-not $Relaunched)
{
	$argList = @(
		"-NoExit",
		"-ExecutionPolicy", "Bypass",
		"-File", "`"$PSCommandPath`"",
		"-SketchName", "`"$SketchName`"",
		"-RepoRoot", "`"$RepoRoot`"",
		"-Relaunched"
	)
	if ($IncludeLegacyAssets)
	{
		$argList += "-IncludeLegacyAssets"
	}

	Start-Process -FilePath "powershell.exe" -ArgumentList $argList -WorkingDirectory $RepoRoot
	exit 0
}

function Get-EffectiveVersion([string]$sketchDir, [string]$repoRoot)
{
	$versionTxtPath = Join-Path $sketchDir "version.txt"
	if (-not (Test-Path $versionTxtPath))
	{
		throw "Could not find version.txt at '$versionTxtPath'."
	}

	# version.txt is a quoted string literal, e.g. "2.0"
	$sketchVersion = (Get-Content $versionTxtPath -Raw).Trim().Trim('"')

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

function Get-BoardId([string]$sketchDir, [string]$sketchName)
{
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
	if ($boardId -ne 'ESP32S3_DEV')
	{
		return $boardId
	}

	# ESP32S3_DEV is ambiguous (shared by multiple wiring variants); disambiguate via the
	# sketch's .ino variant #define, if present.
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
	# "_PLAYGROUND" suffix.
	return 'ESP32S3_DEV_PLAYGROUND'
}

Write-Host "Publishing release for sketch '$SketchName'..."

$sketchDir = Join-Path $RepoRoot $SketchName
if (-not (Test-Path $sketchDir))
{
	throw "Sketch directory not found: '$sketchDir'."
}

$version = Get-EffectiveVersion -sketchDir $sketchDir -repoRoot $RepoRoot
Write-Host "Effective version: $version"

$binFile = Find-LatestBin -sketchDir $sketchDir -sketchName $SketchName
Write-Host "Using firmware: $($binFile.FullName) (built $($binFile.LastWriteTime))"

$boardId = Get-BoardId -sketchDir $sketchDir -sketchName $SketchName
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
gh release view $tag --repo scottwoyak/Arduino *> $null
if ($LASTEXITCODE -ne 0)
{
	$releaseExists = $false
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
