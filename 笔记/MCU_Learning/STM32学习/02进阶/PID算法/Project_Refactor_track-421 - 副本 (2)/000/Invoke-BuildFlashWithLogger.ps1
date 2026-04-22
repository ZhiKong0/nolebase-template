[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [string]$BuildFlashScript,
    [string]$LoggerScript,
    [string]$PowerShellPath,

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
    [switch]$SkipReset,

    [string]$LoggerPort = "COM18",
    [int]$LoggerBaud = 115200,
    [string]$LoggerOutDir,
    [double]$LoggerMaxSeconds = 0.0,
    [switch]$LoggerEcho,
    [switch]$SkipLogger,
    [switch]$ForceNewLogger,
    [string]$LoggerWindowTitle = "Experiment Logger - track-421",
    [int]$LoggerStartupDelayMs = 800
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

if ([string]::IsNullOrWhiteSpace($BuildFlashScript)) {
    $BuildFlashScript = Join-Path $scriptRoot "Invoke-BuildFlash.ps1"
}

if ([string]::IsNullOrWhiteSpace($LoggerScript)) {
    $LoggerScript = Join-Path $ProjectRoot "000Project_PC_Control\experiment_logger.ps1"
}

if ([string]::IsNullOrWhiteSpace($LoggerOutDir)) {
    $LoggerOutDir = Join-Path $ProjectRoot "000Data\serial_runs\experiments"
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

function ConvertTo-PsSingleQuoted {
    param([string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

function Get-ExistingLoggerProcess {
    param([string]$Port)

    try {
        return @(Get-CimInstance Win32_Process | Where-Object {
            $_.CommandLine `
                -and $_.CommandLine -match "experiment_logger\.(py|ps1)" `
                -and $_.CommandLine -match [regex]::Escape($Port)
        })
    } catch {
        Write-Warning ("Failed to query existing logger process: {0}" -f $_.Exception.Message)
        return @()
    }
}

function Start-LoggerWindow {
    param(
        [string]$ShellPath,
        [string]$LoggerLauncher,
        [string]$Port,
        [int]$Baud,
        [string]$OutDir,
        [double]$MaxSeconds,
        [bool]$EchoEnabled,
        [string]$WindowTitle
    )

    $commandParts = @()
    $commandParts += ('$Host.UI.RawUI.WindowTitle = {0}' -f (ConvertTo-PsSingleQuoted $WindowTitle))

    $loggerInvoke = @('&', (ConvertTo-PsSingleQuoted $LoggerLauncher), '--port', (ConvertTo-PsSingleQuoted $Port), '--baud', $Baud, '--out', (ConvertTo-PsSingleQuoted $OutDir))
    if ($EchoEnabled) {
        $loggerInvoke += '--echo'
    }
    if ($MaxSeconds -gt 0.0) {
        $loggerInvoke += @('--max-seconds', [string]$MaxSeconds)
    }
    $commandParts += ($loggerInvoke -join ' ')

    $command = '& { ' + ($commandParts -join '; ') + ' }'

    return Start-Process -FilePath $ShellPath -ArgumentList @(
        '-NoLogo',
        '-ExecutionPolicy', 'Bypass',
        '-Command', $command
    ) -PassThru
}

$projectRootResolved = (Resolve-Path $ProjectRoot).Path
$buildFlashResolved = (Resolve-Path $BuildFlashScript).Path
$loggerScriptResolved = (Resolve-Path $LoggerScript).Path
$loggerOutResolved = if (Test-Path $LoggerOutDir) {
    (Resolve-Path $LoggerOutDir).Path
} else {
    (New-Item -ItemType Directory -Path $LoggerOutDir -Force).FullName
}
$powerShellResolved = Resolve-ToolPath -PreferredPath $PowerShellPath -FallbackCommand "powershell.exe" -ToolName "PowerShell"

if (-not (Test-Path $buildFlashResolved)) {
    throw ("Build/flash script not found: {0}" -f $buildFlashResolved)
}

if (-not (Test-Path $loggerScriptResolved)) {
    throw ("Logger script not found: {0}" -f $loggerScriptResolved)
}

Write-Step "Configuration"
Write-Host ("ProjectRoot      : {0}" -f $projectRootResolved)
Write-Host ("BuildFlashScript : {0}" -f $buildFlashResolved)
Write-Host ("LoggerScript     : {0}" -f $loggerScriptResolved)
Write-Host ("LoggerPort       : {0}" -f $LoggerPort)
Write-Host ("LoggerBaud       : {0}" -f $LoggerBaud)
Write-Host ("LoggerOutDir     : {0}" -f $loggerOutResolved)
Write-Host ("LoggerWindow     : {0}" -f $LoggerWindowTitle)

if (-not $SkipLogger -and -not $ProbeOnly) {
    Write-Step "Logger"
    $existingLogger = @()
    if (-not $ForceNewLogger) {
        $existingLogger = @(Get-ExistingLoggerProcess -Port $LoggerPort)
    }

    if ($existingLogger.Count -gt 0) {
        $pidList = ($existingLogger | Select-Object -ExpandProperty ProcessId) -join ", "
        Write-Host ("Reuse existing logger on {0}: PID {1}" -f $LoggerPort, $pidList)
    } else {
        $loggerProc = Start-LoggerWindow `
            -ShellPath $powerShellResolved `
            -LoggerLauncher $loggerScriptResolved `
            -Port $LoggerPort `
            -Baud $LoggerBaud `
            -OutDir $loggerOutResolved `
            -MaxSeconds $LoggerMaxSeconds `
            -EchoEnabled $LoggerEcho.IsPresent `
            -WindowTitle $LoggerWindowTitle
        Write-Host ("Started logger window: PID {0}" -f $loggerProc.Id)
        if ($LoggerStartupDelayMs -gt 0) {
            Start-Sleep -Milliseconds $LoggerStartupDelayMs
        }
    }
} elseif ($SkipLogger) {
    Write-Step "Logger"
    Write-Host "Skip logger by request."
}

Write-Step "Build/Flash"
$buildParams = @{
    ProjectRoot = $projectRootResolved
    Uv4Path = $Uv4Path
    PyocdPath = $PyocdPath
    Target = $Target
    PyocdTarget = $PyocdTarget
    PyocdFreqHz = $PyocdFreqHz
    Image = $Image
}

if (-not [string]::IsNullOrWhiteSpace($ProbeUid)) {
    $buildParams.ProbeUid = $ProbeUid
}
if ($BuildOnly) {
    $buildParams.BuildOnly = $true
}
if ($FlashOnly) {
    $buildParams.FlashOnly = $true
}
if ($ProbeOnly) {
    $buildParams.ProbeOnly = $true
}
if ($SkipReset) {
    $buildParams.SkipReset = $true
}

& $buildFlashResolved @buildParams
if ($LASTEXITCODE -ne 0) {
    throw ("Build/flash wrapper failed with exit code {0}" -f $LASTEXITCODE)
}

Write-Step "Done"
Write-Host "Build/flash with logger flow completed successfully."
