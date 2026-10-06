<#
.SYNOPSIS
	Uploads the compiled firmware for a single Arduino sketch to the Device Hub server
	(DeviceServer).

.DESCRIPTION
	1. Locates the sketch's most recently built <SketchName>.ino.bin under
	   <SketchName>\build.
	2. Computes the effective version (sketch MakeVersion("...") literal + LIBRARY_VERSION),
	   matching the compile-time MakeVersion() logic.
	3. Resolves the board id from the binary (ARDUINO_BOARD_VARIANT_ID).
	4. Prompts for confirmation, then POSTs multipart/form-data to
	   <Server>/api/firmware/<SketchName>.<boardId> with fields 'version', 'notes'
	   and 'binary'.

	The firmware name on the server is "<SketchName>.<boardId>" so a sketch built for
	several boards is stored independently, like the board-qualified GitHub assets.

	Uses curl.exe (included with Windows) for the multipart upload, so it works in
	Windows PowerShell 5.1 as well as PowerShell 7.

	VISUAL STUDIO EXTERNAL TOOL:
	  Title:       Publish To Hub
	  Command:     powershell.exe
	  Arguments:   -ExecutionPolicy Bypass -NoExit -File "$(SolutionDir)Tools\Publish-ToHub.ps1" -SketchName $(ProjectName)
	  Initial directory: $(ProjectDir)

.PARAMETER SketchName
	Name of the sketch to publish. Defaults to the current directory's name.

.PARAMETER RepoRoot
	Root of the Arduino repo. Defaults to the parent of this script's directory.

.PARAMETER Server
	Base URL of the Device Hub server. Defaults to production.

.PARAMETER Token
	Optional bearer token, sent as an Authorization header if supplied. The server's
	firmware endpoint currently requires none.

.PARAMETER Notes
	Optional release notes stored with the firmware.

.PARAMETER Force
	Skips the confirmation prompt.

.EXAMPLE
	.\Publish-ToHub.ps1 -SketchName Gate_Opener
#>

param(
	[string]$SketchName = (Split-Path -Leaf (Get-Location)),
	[string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
	[string]$Server = "https://devices.littlebear.dev",
	[string]$Token,
	[string]$Notes,
	[switch]$Force,
	[switch]$Relaunched,
	[switch]$SketchNameWasDefaulted
)

$ErrorActionPreference = "Stop"

# External Tools launches without an interactive console, so relaunch in a real console
# window. SketchName is forwarded explicitly since the working
# directory changes; the defaulted flag preserves whether the caller specified it.
if (-not $Relaunched)
{
	$SketchNameWasDefaulted = -not $PSBoundParameters.ContainsKey('SketchName')

	$argList = @(
		"-ExecutionPolicy", "Bypass",
		"-File", "`"$PSCommandPath`"",
		"-SketchName", "`"$SketchName`"",
		"-RepoRoot", "`"$RepoRoot`"",
		"-Server", "`"$Server`"",
		"-Relaunched"
	)
	if ($SketchNameWasDefaulted)
	{
		$argList += "-SketchNameWasDefaulted"
	}
	if ($Token)
	{
		$argList += @("-Token", "`"$Token`"")
	}
	if ($Notes)
	{
		$argList += @("-Notes", "`"$Notes`"")
	}
	if ($Force)
	{
		$argList += "-Force"
	}

	$process = Start-Process -FilePath "powershell.exe" -ArgumentList $argList -WorkingDirectory $RepoRoot -Wait -PassThru
	exit $process.ExitCode
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
	public static object[] GetRunningDTEs(out string error)
	{
		var results = new System.Collections.Generic.List<object>();
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
							results.Add(obj);
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

				return results.ToArray();
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
	$all = [RotHelper]::GetRunningDTEs([ref]$error)
	if (-not $all -or $all.Count -eq 0)
	{
		Write-Warning "Get-RunningDTE: $error"
		return $null
	}

	# Several Visual Studio instances may be running (e.g. DeviceHub and Arduino);
	# prefer the one whose solution lives under this repo.
	$repoRoot = (Split-Path -Parent $PSScriptRoot).TrimEnd('\') + '\'
	foreach ($candidate in $all)
	{
		try
		{
			if ($candidate.Solution.FullName -like "$repoRoot*")
			{
				return $candidate
			}
		}
		catch { }
	}

	Write-Warning "Get-RunningDTE: no running Visual Studio has a solution under $repoRoot"
	return $null
}

function Get-StartupProjectDirectory
{
	# Visual Studio's $(ProjectDir) External Tools macro is resolved from the active
	# *document* (tab), i.e. whichever file happens to be displayed/focused, not from
	# the solution's configured startup project. That makes publishing accidentally
	# depend on which file tab was last clicked (e.g. a library header), rather than
	# which sketch the user actually intends to build/deploy. Instead, ask the running
	# Visual Studio instance directly, via its DTE automation object, for the solution's
	# startup project (Project > Set as Startup Project), which is a stable, explicit
	# choice independent of the active document/tab. If multiple Visual Studio instances
	# are running, an arbitrary one is used.
	$dte = Get-RunningDTE
	if (-not $dte)
	{
		return $null
	}

	try
	{
		$startupProjectNames = $dte.Solution.SolutionBuild.StartupProjects
		if ($startupProjectNames)
		{
			foreach ($uniqueName in $startupProjectNames)
			{
				foreach ($project in $dte.Solution.Projects)
				{
					if ($project -and $project.UniqueName -eq $uniqueName -and $project.FullName)
					{
						return (Split-Path -Parent $project.FullName)
					}
				}
			}
		}

		Write-Warning "Get-StartupProjectDirectory: DTE.Solution.SolutionBuild.StartupProjects is empty or unresolved (no startup project set)."
	}
	catch
	{
		Write-Warning "Get-StartupProjectDirectory: failed to read StartupProjects: $($_.Exception.Message)"
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

	return "$sketchVersion.$($match.Matches[0].Groups[1].Value)"
}

function Find-LatestBin([string]$sketchDir, [string]$sketchName)
{
	$buildsRoot = Join-Path $sketchDir "build"
	if (-not (Test-Path $buildsRoot))
	{
		throw "No local build output found for '$sketchName' under '$buildsRoot'. Build the sketch in Visual Studio first."
	}

	$binName = "$sketchName.ino.bin"
	$candidates = Get-ChildItem -Path $buildsRoot -Recurse -Filter $binName -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -notlike (Join-Path $buildsRoot "_publish\*") }
	if (-not $candidates)
	{
		throw "No '$binName' found under '$buildsRoot'. Build the sketch in Visual Studio first."
	}

	return ($candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1)
}

function Get-BoardIdFromBinary([string]$binPath)
{
	# Scans the .bin for the embedded ARDUINO_BOARD_VARIANT_ID string (see ArduinoBoard.h).
	$text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($binPath))

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

trap
{
	Write-Host ""
	Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
	Read-Host "Press Enter to close this window"
	exit 1
}

$sketchDir = Join-Path $RepoRoot $SketchName
if ($SketchNameWasDefaulted)
{
	# Prefer the solution's startup project over the active document tab.
	$startupProjectDir = Get-StartupProjectDirectory
	if ($startupProjectDir -and (Test-Path (Join-Path $startupProjectDir "$(Split-Path -Leaf $startupProjectDir).ino")))
	{
		$SketchName = Split-Path -Leaf $startupProjectDir
		$sketchDir = $startupProjectDir
		Write-Host "Resolved sketch from Visual Studio's startup project: '$SketchName'."
	}
}

if (-not (Test-Path $sketchDir))
{
	throw "Sketch folder '$sketchDir' not found."
}

$version = Get-EffectiveVersion -sketchDir $sketchDir -sketchName $SketchName -repoRoot $RepoRoot
$bin = Find-LatestBin -sketchDir $sketchDir -sketchName $SketchName
$boardId = Get-BoardIdFromBinary -binPath $bin.FullName
if (-not $boardId)
{
	throw "Could not find a known ARDUINO_BOARD_VARIANT_ID in '$($bin.FullName)'."
}

$firmwareName = "$SketchName.$boardId"
$baseUrl = $Server.TrimEnd('/')
$uploadUrl = "$baseUrl/api/firmware/$([uri]::EscapeDataString($firmwareName))"

Write-Host "Sketch:   $SketchName"
Write-Host "Board:    $boardId"
Write-Host "Version:  $version"
Write-Host "Binary:   $($bin.FullName) ($($bin.Length) bytes, built $($bin.LastWriteTime))"
Write-Host "Firmware: $firmwareName"
Write-Host "Upload:   $uploadUrl"
Write-Host ""

if (-not $Force)
{
	$answer = Read-Host "Upload to Device Hub? (y/N)"
	if ($answer -notmatch '^[yY]')
	{
		Write-Host "Cancelled."
		exit 0
	}
}

$curlArgs = @("--silent", "--show-error", "--fail-with-body")
if ($Token)
{
	$curlArgs += @("-H", "Authorization: Bearer $Token")
}
$binText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($bin.FullName))
$buildMatch = [regex]::Match($binText, 'BUILD@([A-Z][a-z]{2} [ 0-9]\d \d{4} \d\d:\d\d:\d\d)')
$curlArgs += @("-F", "version=$version")
if ($buildMatch.Success) { $curlArgs += @("-F", "buildTime=$($buildMatch.Groups[1].Value)") }
if ($Notes)
{
	$curlArgs += @("-F", "notes=$Notes")
}
$curlArgs += @("-F", "binary=@$($bin.FullName);type=application/octet-stream", $uploadUrl)

$response = & curl.exe @curlArgs
if ($LASTEXITCODE -ne 0)
{
	throw "Upload failed (curl exit code $LASTEXITCODE): $response"
}

Write-Host "Uploaded:" -ForegroundColor Green
Write-Host $response
Write-Host "Download URL: $baseUrl/api/firmware/$([uri]::EscapeDataString($firmwareName))/$([uri]::EscapeDataString($version))/download"
