<#
.SYNOPSIS
	Uploads the latest compiled firmware of every OTA-enabled sketch to the Device Hub
	server (DeviceServer).

.DESCRIPTION
	Intended to be run after an overnight compile. For each sketch folder whose .ino
	enables OTA ("enableOTA = true", or uses TempMonitorSketch which enables it
	internally), finds every <SketchName>.ino.bin under <SketchName>\build, keeps the
	newest binary per board id, computes the effective version exactly as
	Publish-ToHub.ps1 does, and POSTs each to <Server>/api/firmware/<SketchName>.<boardId>.

	Sketches with no build output are reported and skipped. A failure on one upload does
	not stop the others. A summary is printed at the end.

.PARAMETER RepoRoot
	Root of the Arduino repo. Defaults to the parent of this script's directory.

.PARAMETER Server
	Base URL of the Device Hub server. Defaults to production.

.PARAMETER Token
	Optional bearer token, sent as an Authorization header if supplied.

.PARAMETER Notes
	Optional release notes stored with every uploaded firmware.

.PARAMETER Force
	Skips the confirmation prompt.

.PARAMETER ListOnly
	Shows what would be uploaded without uploading anything.

.PARAMETER Reupload
	Skips the check for versions already on the server and uploads everything found.
	Without it, any firmware whose computed version is already published is skipped.

.EXAMPLE
	.\Publish-AllToHub.ps1 -ListOnly

.EXAMPLE
	.\Publish-AllToHub.ps1 -Force
#>

param(
	[string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
	[string]$Server = "https://devices.littlebear.dev",
	[string]$Token,
	[string]$Notes,
	[switch]$Force,
	[switch]$ListOnly,
	[switch]$NoPause,
	[switch]$Reupload
)

$ErrorActionPreference = "Stop"

function Wait-BeforeExit
{
	if (-not $NoPause)
	{
		Read-Host "Press Enter to close" | Out-Null
	}
}

trap
{
	Write-Host "ERROR: $_" -ForegroundColor Red
	Wait-BeforeExit
	exit 1
}

$knownBoardIds = @(
	'ADAFRUIT_FEATHER_M0',
	'ADAFRUIT_FEATHER_ESP32S3_TFT',
	'WAVESHARE_ESP32_S3_ZERO_SENSORS',
	'WAVESHARE_ESP32_S3_ZERO',
	'WAVESHARE_ESP32S3_TOUCH_LCD_43',
	'HOSYOND_ESP32_S3_VIEWER',
	'ESP32S3_DEV_PLAYGROUND'
)

function Get-LibraryVersion([string]$repoRoot)
{
	$path = Join-Path $repoRoot "libraries\Woyak\LibraryVersion.h"
	$match = Select-String -Path $path -Pattern 'LIBRARY_VERSION\s*=\s*"([^"]+)"' | Select-Object -First 1
	if (-not $match)
	{
		throw "Could not parse LIBRARY_VERSION from '$path'."
	}
	return $match.Matches[0].Groups[1].Value
}

function Test-SupportsOta([string]$inoPath)
{
	$text = Get-Content -Path $inoPath -Raw
	return ($text -match 'enableOTA\s*=\s*true') -or ($text -match 'TempMonitorSketch')
}

function Get-BoardIdFromBinary([string]$binPath)
{
	$text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($binPath))
	foreach ($candidate in $knownBoardIds)
	{
		if ($text -match "[^A-Za-z0-9_]$candidate[^A-Za-z0-9_]")
		{
			return $candidate
		}
	}
	return $null
}

function Test-AlreadyPublished($upload)
{
	$checkUrl = "$baseUrl/api/firmware/$([uri]::EscapeDataString($upload.Firmware))/$([uri]::EscapeDataString($upload.Version))/download"
	$checkArgs = @("--silent", "--output", "NUL", "--write-out", "%{http_code}", "--max-time", "60")
	if ($Token)
	{
		$checkArgs += @("-H", "Authorization: Bearer $Token")
	}
	$checkArgs += $checkUrl
	$code = (& curl.exe @checkArgs 2>&1) -join ""
	return ($code -eq "200")
}

$libraryVersion = Get-LibraryVersion -repoRoot $RepoRoot
$libraryVersionTime = (Get-Item (Join-Path $RepoRoot "libraries\Woyak\LibraryVersion.h")).LastWriteTime
$baseUrl = $Server.TrimEnd('/')
$results = @()
$uploads = @()

Write-Host "Scanning sketches for compiled firmware..."

foreach ($dir in (Get-ChildItem -Path $RepoRoot -Directory | Sort-Object Name))
{
	$name = $dir.Name
	$inoPath = Join-Path $dir.FullName "$name.ino"
	if (-not (Test-Path $inoPath) -or -not (Test-SupportsOta $inoPath))
	{
		continue
	}

	Write-Host "  Scanning $name..."

	$buildsRoot = Join-Path $dir.FullName "build"
	$bins = @()
	if (Test-Path $buildsRoot)
	{
		$bins = @(Get-ChildItem -Path $buildsRoot -Recurse -Filter "$name.ino.bin" -ErrorAction SilentlyContinue |
			Where-Object { $_.FullName -notlike (Join-Path $buildsRoot "_publish\*") } |
			Sort-Object LastWriteTime -Descending)
	}
	if ($bins.Count -eq 0)
	{
		$results += [pscustomobject]@{ Sketch = $name; Status = "SKIPPED"; Detail = "no build output" }
		continue
	}

	$versionMatch = Select-String -Path $inoPath -Pattern 'MakeVersion\("([^"]+)"\)' | Select-Object -First 1
	if (-not $versionMatch)
	{
		$results += [pscustomobject]@{ Sketch = $name; Status = "SKIPPED"; Detail = "no MakeVersion(...) literal" }
		continue
	}
	$version = "$($versionMatch.Matches[0].Groups[1].Value).$libraryVersion"

	# A binary older than the sketch or LibraryVersion.h was compiled with a different version.
	$sourceTime = (Get-Item $inoPath).LastWriteTime
	if ($libraryVersionTime -gt $sourceTime)
	{
		$sourceTime = $libraryVersionTime
	}

	# Newest binary per board id (bins are already sorted newest first).
	$seenBoards = @{}
	foreach ($bin in $bins)
	{
		$boardId = Get-BoardIdFromBinary -binPath $bin.FullName
		if (-not $boardId)
		{
			$results += [pscustomobject]@{ Sketch = $name; Status = "SKIPPED"; Detail = "unknown board id in $($bin.FullName)" }
			continue
		}
		if ($seenBoards.ContainsKey($boardId))
		{
			continue
		}
		$seenBoards[$boardId] = $true

		if ($bin.LastWriteTime -lt $sourceTime)
		{
			$results += [pscustomobject]@{ Sketch = $name; Status = "STALE"; Detail = "$boardId not rebuilt since $sourceTime" }
			continue
		}

		$upload = [pscustomobject]@{
			Sketch   = $name
			Board    = $boardId
			Version  = $version
			Built    = $bin.LastWriteTime
			Bin      = $bin.FullName
			Firmware = "$name.$boardId"
		}

		if (-not $Reupload -and (Test-AlreadyPublished $upload))
		{
			$results += [pscustomobject]@{ Sketch = $name; Status = "UP TO DATE"; Detail = "$($upload.Firmware) $version" }
			continue
		}

		$uploads += $upload
		Write-Host "    $($upload.Firmware) $version" -ForegroundColor Green
	}
}

Write-Host ""
if ($uploads.Count -eq 0)
{
	Write-Host "Nothing needs to be uploaded." -ForegroundColor Yellow
}
else
{
	Write-Host "Ready to upload to ${baseUrl}:"
	foreach ($u in $uploads)
	{
		Write-Host "  $($u.Firmware) $($u.Version)" -ForegroundColor Green
	}
}

if ($uploads.Count -eq 0 -or $ListOnly)
{
	foreach ($r in ($results | Where-Object { $_.Status -eq "SKIPPED" })) { Write-Host "SKIPPED $($r.Sketch): $($r.Detail)" -ForegroundColor Yellow }
	Wait-BeforeExit
	exit 0
}

if (-not $Force)
{
	$answer = Read-Host "Upload $($uploads.Count) firmware(s)? (y/N)"
	if ($answer -notmatch '^[yY]')
	{
		Write-Host "Cancelled."
		Wait-BeforeExit
		exit 0
	}
}

foreach ($u in $uploads)
{
	$uploadUrl = "$baseUrl/api/firmware/$([uri]::EscapeDataString($u.Firmware))"
	$curlArgs = @("--silent", "--show-error", "--fail-with-body")
	if ($Token)
	{
		$curlArgs += @("-H", "Authorization: Bearer $Token")
	}
	$curlArgs += @("-F", "version=$($u.Version)")
	if ($Notes)
	{
		$curlArgs += @("-F", "notes=$Notes")
	}
	$curlArgs += @("-F", "binary=@$($u.Bin);type=application/octet-stream", $uploadUrl)

	$response = & curl.exe @curlArgs 2>&1
	if ($LASTEXITCODE -eq 0)
	{
		Write-Host "OK     $($u.Firmware) $($u.Version)" -ForegroundColor Green
		$results += [pscustomobject]@{ Sketch = $u.Sketch; Status = "UPLOADED"; Detail = "$($u.Firmware) $($u.Version)" }
	}
	else
	{
		Write-Host "FAILED $($u.Firmware): $response" -ForegroundColor Red
		$results += [pscustomobject]@{ Sketch = $u.Sketch; Status = "FAILED"; Detail = "$response" }
	}
}

Write-Host ""
Write-Host "Summary"
$results | Sort-Object Status, Sketch | Format-Table Sketch, Status, Detail -AutoSize | Out-String | Write-Host

Wait-BeforeExit

if ($results | Where-Object { $_.Status -eq "FAILED" })
{
	exit 1
}
