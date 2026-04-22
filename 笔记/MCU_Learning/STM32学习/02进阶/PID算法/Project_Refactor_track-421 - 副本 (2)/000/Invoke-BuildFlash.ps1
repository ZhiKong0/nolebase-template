[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [string]$Uv4Path = "D:\keil\Keil-v5\Arm\UV4\UV4.exe",
    [string]$PyocdPath = "C:\Users\DZ\AppData\Local\Programs\Python\Python313\Scripts\pyocd.exe",
    [string]$Target = "Target 1",
    [string]$PyocdTarget = "stm32f103rc",
    [int]$PyocdFreqHz = 10000000,
    [string]$ProbeUid,
    [ValidateSet("Auto", "Hex", "Axf")]
    [string]$Image = "Auto",
    [switch]$BuildOnly,
    [switch]$FlashOnly,
    [switch]$ProbeOnly,
    [switch]$SkipReset
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$global:LASTEXITCODE = 0

$scriptRoot = if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) {
    $PSScriptRoot
} else {
    Split-Path -Parent $MyInvocation.MyCommand.Path
}

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = (Resolve-Path (Join-Path $scriptRoot "..")).Path
}

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host ("=== {0} ===" -f $Message) -ForegroundColor Cyan
}

function Resolve-ToolPath {
    param(
        [string]$PreferredPath,
        [string]$FallbackCommand,
        [string]$ToolName
    )

    if (-not [string]::IsNullOrWhiteSpace($PreferredPath) -and (Test-Path $PreferredPath)) {
        return (Resolve-Path $PreferredPath).Path
    }

    if (-not [string]::IsNullOrWhiteSpace($FallbackCommand)) {
        $command = Get-Command $FallbackCommand -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            return $command.Source
        }
    }

    throw ("{0} not found. Preferred path: {1}" -f $ToolName, $PreferredPath)
}

function Invoke-External {
    param(
        [string]$ExePath,
        [string[]]$Arguments,
        [string]$StepName
    )

    Write-Host ("Command: & '{0}' {1}" -f $ExePath, ($Arguments -join " "))
    & $ExePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("{0} failed with exit code {1}" -f $StepName, $LASTEXITCODE)
    }
}

function Get-BuildLogText {
    param([string]$LogPath)

    if (-not (Test-Path $LogPath)) {
        throw ("Build log not found: {0}" -f $LogPath)
    }

    return Get-Content -Raw -Encoding Default $LogPath
}

function Get-LatestSourceItem {
    param([string]$RootPath)

    $sourceDirs = @("User", "Hardware", "System") |
        ForEach-Object { Join-Path $RootPath $_ } |
        Where-Object { Test-Path $_ }

    if ($sourceDirs.Count -eq 0) {
        return $null
    }

    return Get-ChildItem $sourceDirs -Recurse -Include *.c, *.h |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

function Get-ImageCandidates {
    param(
        [string]$HexPath,
        [string]$AxfPath
    )

    $candidates = @()

    if (Test-Path $HexPath) {
        $hexItem = Get-Item $HexPath
        $candidates += [pscustomobject]@{
            Kind = "Hex"
            Path = $hexItem.FullName
            LastWriteTime = $hexItem.LastWriteTime
        }
    }

    if (Test-Path $AxfPath) {
        $axfItem = Get-Item $AxfPath
        $candidates += [pscustomobject]@{
            Kind = "Axf"
            Path = $axfItem.FullName
            LastWriteTime = $axfItem.LastWriteTime
        }
    }

    return $candidates
}

function Select-FlashImage {
    param(
        [string]$Mode,
        [object[]]$Candidates,
        [object]$LatestSource
    )

    if ($Candidates.Count -eq 0) {
        throw "No flashable artifact found in Objects."
    }

    $freshCandidates = if ($null -eq $LatestSource) {
        $Candidates
    } else {
        $Candidates | Where-Object { $_.LastWriteTime -gt $LatestSource.LastWriteTime }
    }

    if ($freshCandidates.Count -eq 0) {
        throw ("No artifact is newer than source: {0} ({1:yyyy-MM-dd HH:mm:ss})" -f $LatestSource.FullName, $LatestSource.LastWriteTime)
    }

    switch ($Mode) {
        "Hex" {
            $selected = $freshCandidates | Where-Object { $_.Kind -eq "Hex" } | Select-Object -First 1
            if ($null -eq $selected) {
                throw "Requested Hex image, but no fresh HEX artifact is available."
            }
            return $selected
        }
        "Axf" {
            $selected = $freshCandidates | Where-Object { $_.Kind -eq "Axf" } | Select-Object -First 1
            if ($null -eq $selected) {
                throw "Requested Axf image, but no fresh AXF artifact is available."
            }
            return $selected
        }
        default {
            $preferredOrder = @("Hex", "Axf")
            foreach ($kind in $preferredOrder) {
                $selected = $freshCandidates | Where-Object { $_.Kind -eq $kind } | Select-Object -First 1
                if ($null -ne $selected) {
                    return $selected
                }
            }
        }
    }

    throw "Unable to select a flash image."
}

if ($BuildOnly -and $FlashOnly) {
    throw "-BuildOnly and -FlashOnly cannot be used together."
}

if ($ProbeOnly -and ($BuildOnly -or $FlashOnly)) {
    throw "-ProbeOnly cannot be combined with -BuildOnly or -FlashOnly."
}

$projectRootResolved = (Resolve-Path $ProjectRoot).Path
$uv4Resolved = Resolve-ToolPath -PreferredPath $Uv4Path -FallbackCommand "UV4.exe" -ToolName "UV4"
$pyocdResolved = Resolve-ToolPath -PreferredPath $PyocdPath -FallbackCommand "pyocd.exe" -ToolName "pyOCD"

$projectFile = Join-Path $projectRootResolved "project.uvprojx"
$objectsDir = Join-Path $projectRootResolved "Objects"
$buildLog = Join-Path $objectsDir "project.build_log.htm"
$hexPath = Join-Path $objectsDir "project.hex"
$axfPath = Join-Path $objectsDir "project.axf"

if (-not (Test-Path $projectFile)) {
    throw ("Keil project file not found: {0}" -f $projectFile)
}

Write-Step "Configuration"
Write-Host ("ProjectRoot : {0}" -f $projectRootResolved)
Write-Host ("ProjectFile : {0}" -f $projectFile)
Write-Host ("UV4         : {0}" -f $uv4Resolved)
Write-Host ("pyOCD       : {0}" -f $pyocdResolved)
Write-Host ("Target      : {0}" -f $Target)
Write-Host ("pyOCD Target: {0}" -f $PyocdTarget)
Write-Host ("Freq (Hz)   : {0}" -f $PyocdFreqHz)
Write-Host ("Image Mode  : {0}" -f $Image)
if (-not [string]::IsNullOrWhiteSpace($ProbeUid)) {
    Write-Host ("Probe UID   : {0}" -f $ProbeUid)
}

if ($ProbeOnly) {
    Write-Step "List Probes"
    Invoke-External -ExePath $pyocdResolved -Arguments @("list", "--probes") -StepName "Probe enumeration"
    return
}

if (-not $FlashOnly) {
    Write-Step "Build"
    Invoke-External -ExePath $uv4Resolved -Arguments @(
        "-b", $projectFile,
        "-j0",
        "-t", $Target,
        "-o", $buildLog
    ) -StepName "Keil build"

    Write-Step "Check Build Log"
    $buildLogText = Get-BuildLogText -LogPath $buildLog
    if ($buildLogText -notmatch "0 Error\(s\)") {
        throw "Build log does not contain 0 Error(s)."
    }
    if ($buildLogText -notmatch "0 Warning\(s\)") {
        Write-Warning "Build log does not contain 0 Warning(s)."
    }
    if ($buildLogText -notmatch "creating hex file") {
        Write-Warning "Build log does not report creating hex file. AXF fallback remains available."
    }
}

$latestSource = Get-LatestSourceItem -RootPath $projectRootResolved
$candidates = Get-ImageCandidates -HexPath $hexPath -AxfPath $axfPath
$selectedImage = Select-FlashImage -Mode $Image -Candidates $candidates -LatestSource $latestSource

Write-Step "Artifact"
if ($null -ne $latestSource) {
    Write-Host ("Latest Src  : {0}" -f $latestSource.FullName)
    Write-Host ("Src Time    : {0:yyyy-MM-dd HH:mm:ss}" -f $latestSource.LastWriteTime)
}
Write-Host ("Image Kind  : {0}" -f $selectedImage.Kind)
Write-Host ("Image Path  : {0}" -f $selectedImage.Path)
Write-Host ("Image Time  : {0:yyyy-MM-dd HH:mm:ss}" -f $selectedImage.LastWriteTime)

if ($BuildOnly) {
    return
}

$probeArgs = @("list", "--probes")

Write-Step "List Probes"
Invoke-External -ExePath $pyocdResolved -Arguments $probeArgs -StepName "Probe enumeration"

$commonPyocdArgs = @("--no-config")
if (-not [string]::IsNullOrWhiteSpace($ProbeUid)) {
    $commonPyocdArgs += @("-u", $ProbeUid)
}
$commonPyocdArgs += @("-t", $PyocdTarget, "-M", "under-reset", "-f", $PyocdFreqHz.ToString())

Write-Step "Erase Chip"
Invoke-External -ExePath $pyocdResolved -Arguments (@("erase", "--chip") + $commonPyocdArgs) -StepName "pyOCD erase"

Write-Step "Load Image"
Invoke-External -ExePath $pyocdResolved -Arguments (@("load") + $commonPyocdArgs + @("-e", "sector", $selectedImage.Path)) -StepName "pyOCD load"

if (-not $SkipReset) {
    Write-Step "Reset Target"
    $resetArgs = @("reset", "--no-config")
    if (-not [string]::IsNullOrWhiteSpace($ProbeUid)) {
        $resetArgs += @("-u", $ProbeUid)
    }
    $resetArgs += @("-t", $PyocdTarget)
    Invoke-External -ExePath $pyocdResolved -Arguments $resetArgs -StepName "pyOCD reset"
}

Write-Step "Done"
Write-Host "Build/flash flow completed successfully."
