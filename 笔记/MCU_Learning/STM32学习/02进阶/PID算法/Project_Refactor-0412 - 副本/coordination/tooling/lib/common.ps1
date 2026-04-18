Set-StrictMode -Version Latest

function ConvertTo-PlainObject {
  param(
    [Parameter(ValueFromPipeline = $true)]
    $Value
  )

  if ($null -eq $Value) {
    return $null
  }

  if ($Value -is [System.Management.Automation.PSCustomObject]) {
    $result = @{}
    foreach ($property in $Value.PSObject.Properties) {
      $result[$property.Name] = ConvertTo-PlainObject $property.Value
    }
    return $result
  }

  if ($Value -is [System.Collections.IDictionary]) {
    $result = @{}
    foreach ($key in $Value.Keys) {
      $result[$key] = ConvertTo-PlainObject $Value[$key]
    }
    return $result
  }

  if (($Value -is [System.Collections.IEnumerable]) -and -not ($Value -is [string])) {
    $items = @()
    foreach ($item in $Value) {
      $items += ,(ConvertTo-PlainObject $item)
    }
    return $items
  }

  return $Value
}

function Read-JsonFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return @{}
  }

  $raw = Get-Content -LiteralPath $Path -Raw
  if ([string]::IsNullOrWhiteSpace($raw)) {
    return @{}
  }

  return ConvertTo-PlainObject (ConvertFrom-Json $raw)
}

function Merge-Hashtable {
  param(
    [Parameter(Mandatory = $true)]
    [hashtable]$Base,
    [Parameter(Mandatory = $true)]
    [hashtable]$Override
  )

  $result = @{}
  foreach ($key in $Base.Keys) {
    $result[$key] = $Base[$key]
  }

  foreach ($key in $Override.Keys) {
    if (
      $result.ContainsKey($key) -and
      $result[$key] -is [hashtable] -and
      $Override[$key] -is [hashtable]
    ) {
      $result[$key] = Merge-Hashtable -Base $result[$key] -Override $Override[$key]
      continue
    }

    $result[$key] = $Override[$key]
  }

  return $result
}

function Get-OpenClawRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ScriptRoot
  )

  if ($env:COORDINATION_HOST_ROOT -and (Test-Path -LiteralPath $env:COORDINATION_HOST_ROOT)) {
    return (Resolve-Path -LiteralPath $env:COORDINATION_HOST_ROOT).Path
  }

  if ($env:OPENCLAW_ROOT -and (Test-Path -LiteralPath $env:OPENCLAW_ROOT)) {
    return (Resolve-Path -LiteralPath $env:OPENCLAW_ROOT).Path
  }

  $cursor = Resolve-Path -LiteralPath $ScriptRoot
  while ($null -ne $cursor) {
    $coordinationRoot = $cursor.Path
    $systemPath = Join-Path $coordinationRoot "system"
    if ((Split-Path $coordinationRoot -Leaf) -eq "coordination" -and (Test-Path -LiteralPath $systemPath)) {
      $hostRoot = Split-Path -Parent $coordinationRoot
      if (-not [string]::IsNullOrWhiteSpace($hostRoot)) {
        return $hostRoot
      }
    }

    $parent = Split-Path -Parent $coordinationRoot
    if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $coordinationRoot) {
      break
    }
    $cursor = Resolve-Path -LiteralPath $parent
  }

  $candidate = Resolve-Path -LiteralPath (Join-Path $ScriptRoot "..\..\..")
  return $candidate.Path
}

function Get-OpsConfig {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  $examplePath = Join-Path $Root "config\ops.example.json"
  $localPath = Join-Path $Root "config\ops.local.json"
  $exampleConfig = Read-JsonFile -Path $examplePath
  $localConfig = Read-JsonFile -Path $localPath
  return Merge-Hashtable -Base $exampleConfig -Override $localConfig
}

function Get-ConfigValue {
  param(
    [Parameter(Mandatory = $true)]
    [hashtable]$Config,
    [Parameter(Mandatory = $true)]
    [string]$Path,
    $Default = $null
  )

  $current = $Config
  foreach ($part in $Path.Split(".")) {
    if ($current -is [hashtable] -and $current.ContainsKey($part)) {
      $current = $current[$part]
      continue
    }

    return $Default
  }

  if ($null -eq $current) {
    return $Default
  }

  return $current
}

function Resolve-MessageText {
  param(
    [string]$Text,
    [string]$TextFile
  )

  if (-not [string]::IsNullOrWhiteSpace($Text) -and -not [string]::IsNullOrWhiteSpace($TextFile)) {
    throw "Use either -Text or -TextFile, not both."
  }

  if (-not [string]::IsNullOrWhiteSpace($TextFile)) {
    if (-not (Test-Path -LiteralPath $TextFile)) {
      throw "Text file not found: $TextFile"
    }

    $content = Get-Content -LiteralPath $TextFile -Raw
    if ([string]::IsNullOrWhiteSpace($content)) {
      throw "Text file is empty: $TextFile"
    }

    return $content.Trim()
  }

  if ([string]::IsNullOrWhiteSpace($Text)) {
    throw "Missing message text. Pass -Text or -TextFile."
  }

  return $Text.Trim()
}

function Ensure-Directory {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
  }
}

function New-AtomicTempPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  $parent = Split-Path -Parent $Path
  if ([string]::IsNullOrWhiteSpace($parent)) {
    $parent = [System.IO.Path]::GetDirectoryName((Resolve-Path -LiteralPath ".").Path)
  }

  $fileName = [System.IO.Path]::GetFileName($Path)
  $suffix = [guid]::NewGuid().ToString("N")
  return (Join-Path $parent ("." + $fileName + ".tmp." + $suffix))
}

function Get-SidecarLockPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  return ($Path + ".lock")
}

function Invoke-WithFileLock {
  param(
    [Parameter(Mandatory = $true)]
    [string]$LockPath,
    [Parameter(Mandatory = $true)]
    [scriptblock]$ScriptBlock,
    [int]$TimeoutMs = 15000,
    [int]$RetryMs = 100
  )

  $parent = Split-Path -Parent $LockPath
  if (-not [string]::IsNullOrWhiteSpace($parent)) {
    Ensure-Directory -Path $parent
  }

  $deadline = [DateTimeOffset]::Now.AddMilliseconds($TimeoutMs)
  $stream = $null
  while ($true) {
    try {
      $stream = [System.IO.File]::Open($LockPath, [System.IO.FileMode]::OpenOrCreate, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
      break
    }
    catch [System.IO.IOException] {
      if ([DateTimeOffset]::Now -ge $deadline) {
        throw "Timed out waiting for file lock: $LockPath"
      }
      Start-Sleep -Milliseconds $RetryMs
    }
  }

  try {
    return & $ScriptBlock
  }
  finally {
    if ($null -ne $stream) {
      $stream.Dispose()
    }
  }
}

function Write-TextFileAtomic {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    [AllowEmptyString()]
    [string]$Content
  )

  $parent = Split-Path -Parent $Path
  if (-not [string]::IsNullOrWhiteSpace($parent)) {
    Ensure-Directory -Path $parent
  }

  $encoding = New-Object System.Text.UTF8Encoding($false)
  $tempPath = New-AtomicTempPath -Path $Path
  try {
    [System.IO.File]::WriteAllText($tempPath, $Content, $encoding)
    if (Test-Path -LiteralPath $Path) {
      Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
      [System.IO.File]::Move($tempPath, $Path)
    }
    else {
      [System.IO.File]::Move($tempPath, $Path)
    }
  }
  finally {
    if (Test-Path -LiteralPath $tempPath) {
      Remove-Item -LiteralPath $tempPath -Force -ErrorAction SilentlyContinue
    }
  }
}

function Write-TextFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    [AllowEmptyString()]
    [string]$Content
  )

  $parent = Split-Path -Parent $Path
  if (-not [string]::IsNullOrWhiteSpace($parent)) {
    Ensure-Directory -Path $parent
  }

  Invoke-WithFileLock -LockPath (Get-SidecarLockPath -Path $Path) -ScriptBlock {
    Write-TextFileAtomic -Path $Path -Content $Content
  }
}

function Write-JsonFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    $Object
  )

  $json = $Object | ConvertTo-Json -Depth 16
  Write-TextFile -Path $Path -Content ($json + [Environment]::NewLine)
}

function ConvertTo-Slug {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Text
  )

  $value = $Text.ToLowerInvariant()
  $value = [System.Text.RegularExpressions.Regex]::Replace($value, "[^a-z0-9]+", "-")
  $value = $value.Trim("-")

  if ([string]::IsNullOrWhiteSpace($value)) {
    throw "Cannot derive slug from empty text: $Text"
  }

  return $value
}

function Get-OpenClawCmd {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  $path = Join-Path $Root "bin\openclaw.cmd"
  if (-not (Test-Path -LiteralPath $path)) {
    throw "OpenClaw CLI not found at $path"
  }

  return $path
}

function Get-DefaultOpenClawSessionId {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [hashtable]$Config
  )

  $configured = [string](Get-ConfigValue -Config $Config -Path "openclawSession.defaultSessionId" -Default "")
  if (-not [string]::IsNullOrWhiteSpace($configured)) {
    return $configured
  }

  $autoDetect = Get-ConfigValue -Config $Config -Path "openclawSession.autoDetectSessionId" -Default $true
  if (-not $autoDetect) {
    return $null
  }

  $sessionsPath = Join-Path $Root ".openclaw\agents\main\sessions\sessions.json"
  if (-not (Test-Path -LiteralPath $sessionsPath)) {
    return $null
  }

  $sessions = Read-JsonFile -Path $sessionsPath
  if ($sessions.ContainsKey("agent:main:main")) {
    $primary = $sessions["agent:main:main"]
    if ($primary -is [hashtable] -and $primary.ContainsKey("sessionId")) {
      return [string]$primary["sessionId"]
    }
  }

  foreach ($entry in $sessions.GetEnumerator()) {
    if ($entry.Value -is [hashtable] -and $entry.Value.ContainsKey("sessionId")) {
      return [string]$entry.Value["sessionId"]
    }
  }

  return $null
}

function Format-CommandLine {
  param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,
    [string[]]$Arguments = @()
  )

  $parts = @($FilePath) + $Arguments
  $formatted = foreach ($part in $parts) {
    if ($null -eq $part) {
      continue
    }

    $value = [string]$part
    if ($value -match "\s") {
      '"' + ($value -replace '"', '\"') + '"'
    }
    else {
      $value
    }
  }

  return [string]::Join(" ", $formatted)
}

function Invoke-ProcessCapture {
  param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,
    [string[]]$Arguments = @(),
    [switch]$DryRun
  )

  $commandLine = Format-CommandLine -FilePath $FilePath -Arguments $Arguments
  if ($DryRun) {
    return [pscustomobject]@{
      ok = $true
      dryRun = $true
      filePath = $FilePath
      arguments = $Arguments
      commandLine = $commandLine
      exitCode = 0
      stdout = ""
      stderr = ""
    }
  }

  $stdoutPath = [System.IO.Path]::GetTempFileName()
  $stderrPath = [System.IO.Path]::GetTempFileName()

  try {
    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -Wait -NoNewWindow -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $stdout = ""
    if (Test-Path -LiteralPath $stdoutPath) {
      $stdoutRaw = Get-Content -LiteralPath $stdoutPath -Raw
      if ($null -ne $stdoutRaw) {
        $stdout = $stdoutRaw.Trim()
      }
    }

    $stderr = ""
    if (Test-Path -LiteralPath $stderrPath) {
      $stderrRaw = Get-Content -LiteralPath $stderrPath -Raw
      if ($null -ne $stderrRaw) {
        $stderr = $stderrRaw.Trim()
      }
    }

    return [pscustomobject]@{
      ok = ($process.ExitCode -eq 0)
      dryRun = $false
      filePath = $FilePath
      arguments = $Arguments
      commandLine = $commandLine
      exitCode = $process.ExitCode
      stdout = $stdout
      stderr = $stderr
    }
  }
  finally {
    if (Test-Path -LiteralPath $stdoutPath) {
      Remove-Item -LiteralPath $stdoutPath -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $stderrPath) {
      Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
    }
  }
}

function Get-GitCommand {
  $command = Get-Command git -ErrorAction SilentlyContinue
  if (-not $command) {
    throw "git is not available in PATH."
  }

  return $command.Source
}

function Invoke-GitCapture {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoPath,
    [Parameter(Mandatory = $true)]
    [string[]]$Arguments,
    [switch]$DryRun
  )

  $git = Get-GitCommand
  $allArgs = @("-C", $RepoPath) + $Arguments
  return Invoke-ProcessCapture -FilePath $git -Arguments $allArgs -DryRun:$DryRun
}

function Resolve-GitRepositoryRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  $result = Invoke-GitCapture -RepoPath $Path -Arguments @("rev-parse", "--show-toplevel")
  if (-not $result.ok -or [string]::IsNullOrWhiteSpace($result.stdout)) {
    throw "Not a git repository: $Path"
  }

  return $result.stdout.Trim()
}

function Test-GitRef {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoPath,
    [Parameter(Mandatory = $true)]
    [string]$RefName
  )

  $result = Invoke-GitCapture -RepoPath $RepoPath -Arguments @("rev-parse", "--verify", $RefName)
  return $result.ok
}

function Get-DefaultGitBranch {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoPath
  )

  $originHead = Invoke-GitCapture -RepoPath $RepoPath -Arguments @("symbolic-ref", "--quiet", "--short", "refs/remotes/origin/HEAD")
  if ($originHead.ok -and -not [string]::IsNullOrWhiteSpace($originHead.stdout)) {
    $value = $originHead.stdout.Trim()
    if ($value.StartsWith("origin/")) {
      return $value.Substring(7)
    }
    return $value
  }

  $current = Invoke-GitCapture -RepoPath $RepoPath -Arguments @("branch", "--show-current")
  if ($current.ok -and -not [string]::IsNullOrWhiteSpace($current.stdout)) {
    return $current.stdout.Trim()
  }

  return "main"
}

function Get-CoordinationRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path $Root "coordination")
}

function Get-CoordinationRuntimeRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationRoot -Root $Root) "runtime")
}

function Get-CoordinationArchiveRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationRuntimeRoot -Root $Root) "archive")
}

function Get-CoordinationArchivedProjectsRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationArchiveRoot -Root $Root) "projects")
}

function Get-CoordinationArchivedWorktreesRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationArchiveRoot -Root $Root) "worktrees")
}

function Get-CoordinationProjectsRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationRuntimeRoot -Root $Root) "projects")
}

function Get-CoordinationWorktreesRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationRuntimeRoot -Root $Root) "worktrees")
}

function Get-CoordinationSystemRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationRoot -Root $Root) "system")
}

function Get-CoordinationRegistryPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationSystemRoot -Root $Root) "state\registry.json")
}

function Get-CoordinationSystemLocksPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationSystemRoot -Root $Root) "state\locks")
}

function Get-CoordinationProjectSystemLockFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$LockName
  )

  return (Join-Path (Get-CoordinationSystemLocksPath -Root $Root) ("project-" + $ProjectId + "-" + $LockName + ".lock"))
}

function Invoke-WithCoordinationProjectLock {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$LockName,
    [Parameter(Mandatory = $true)]
    [scriptblock]$ScriptBlock,
    [int]$TimeoutMs = 30000
  )

  $coordinationProjectLockPath = Get-CoordinationProjectSystemLockFile -Root $Root -ProjectId $ProjectId -LockName $LockName
  return Invoke-WithFileLock -LockPath $coordinationProjectLockPath -TimeoutMs $TimeoutMs -ScriptBlock $ScriptBlock
}

function Get-CoordinationAiEntryFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationRoot -Root $Root) "AI_ENTRY.md")
}

function Get-CoordinationRuntimeReadmeFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationRuntimeRoot -Root $Root) "README.md")
}

function Get-CoordinationActiveContextMarkdownFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationRuntimeRoot -Root $Root) "ACTIVE_CONTEXT.md")
}

function Get-CoordinationActiveContextJsonFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  return (Join-Path (Get-CoordinationRuntimeRoot -Root $Root) "ACTIVE_CONTEXT.json")
}

function ConvertTo-ObjectArray {
  param(
    $Value
  )

  if ($null -eq $Value) {
    return ,@()
  }

  if (
    $Value -is [hashtable] -or
    $Value -is [System.Management.Automation.PSCustomObject]
  ) {
    return ,@((ConvertTo-PlainObject $Value))
  }

  if (($Value -is [System.Collections.IEnumerable]) -and -not ($Value -is [string])) {
    $items = @()
    foreach ($item in $Value) {
      if ($null -eq $item) {
        continue
      }
      $items += ,(ConvertTo-PlainObject $item)
    }
    return ,@($items)
  }

  return ,@((ConvertTo-PlainObject $Value))
}

function New-CoordinationRegistry {
  return @{
    version = 2
    updatedAt = Get-NowIso
    projects = @()
    archivedProjects = @()
  }
}

function Normalize-CoordinationRegistry {
  param(
    $Registry
  )

  if (-not ($Registry -is [hashtable])) {
    $Registry = @{}
  }

  $Registry["version"] = 2
  if (-not $Registry.ContainsKey("updatedAt") -or [string]::IsNullOrWhiteSpace([string]$Registry["updatedAt"])) {
    $Registry["updatedAt"] = Get-NowIso
  }
  $Registry["projects"] = ConvertTo-ObjectArray -Value $(if ($Registry.ContainsKey("projects")) { $Registry["projects"] } else { @() })
  $Registry["archivedProjects"] = ConvertTo-ObjectArray -Value $(if ($Registry.ContainsKey("archivedProjects")) { $Registry["archivedProjects"] } else { @() })

  return $Registry
}

function Read-CoordinationRegistry {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  $path = Get-CoordinationRegistryPath -Root $Root
  $registry = if (Test-Path -LiteralPath $path) { Read-JsonFile -Path $path } else { @{} }
  return Normalize-CoordinationRegistry -Registry $registry
}

function Write-CoordinationRegistry {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [hashtable]$Registry
  )

  $Registry = Normalize-CoordinationRegistry -Registry $Registry
  $Registry["updatedAt"] = Get-NowIso
  Write-JsonFile -Path (Get-CoordinationRegistryPath -Root $Root) -Object $Registry
}

function Apply-Template {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Template,
    [Parameter(Mandatory = $true)]
    [hashtable]$Tokens
  )

  $result = $Template
  foreach ($key in $Tokens.Keys) {
    $pattern = "{{{{{0}}}}}" -f $key
    $replacement = [string]$Tokens[$key]
    $result = $result.Replace($pattern, $replacement)
  }
  return $result
}

function Read-TemplateFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$RelativePath
  )

  $path = Join-Path (Get-CoordinationSystemRoot -Root $Root) ("templates\project\" + $RelativePath)
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Template file not found: $path"
  }

  return Get-Content -LiteralPath $path -Raw
}

function Get-ProjectCoordinationPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-CoordinationProjectsRoot -Root $Root) $ProjectId)
}

function Get-ProjectContextPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "context")
}

function Get-ProjectBoardFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectContextPath -Root $Root -ProjectId $ProjectId) "BOARD.md")
}

function Get-ProjectHandoffFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectContextPath -Root $Root -ProjectId $ProjectId) "HANDOFF.md")
}

function Get-ProjectDecisionsFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectContextPath -Root $Root -ProjectId $ProjectId) "DECISIONS.md")
}

function Get-ProjectReadmeIndexFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "README_INDEX.md")
}

function Get-ProjectArchitectureIndexFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "ARCHITECTURE_INDEX.md")
}

function Get-ProjectArchitectureTreeFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "ARCHITECTURE_TREE.json")
}

function Get-ProjectBoundariesPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "BOUNDARIES")
}

function Get-ProjectChangelogPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "CHANGELOG_TIER")
}

function Get-ProjectChangelogLevelPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$Level
  )

  return (Join-Path (Get-ProjectChangelogPath -Root $Root -ProjectId $ProjectId) $Level)
}

function Get-ProjectLogsPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "LOGS")
}

function Get-ProjectStreamLogPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "STREAM_LOG")
}

function Get-ProjectStreamNewestEntryFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectStreamLogPath -Root $Root -ProjectId $ProjectId) "NEWEST_ENTRY.json")
}

function Get-ProjectStreamTailFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectStreamLogPath -Root $Root -ProjectId $ProjectId) "LATEST_TAIL.json")
}

function Get-ProjectStreamEntriesFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectStreamLogPath -Root $Root -ProjectId $ProjectId) "STREAM.jsonl")
}

function Get-ProjectSessionHeadsFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectStreamLogPath -Root $Root -ProjectId $ProjectId) "SESSION_HEADS.json")
}

function Get-ProjectStreamReadPointersPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectStreamLogPath -Root $Root -ProjectId $ProjectId) "READ_POINTERS")
}

function Get-ProjectReadPointerFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$AgentId,
    [string]$PointerKey
  )

  $agentSlug = ConvertTo-Slug -Text $AgentId
  $fileName = if ([string]::IsNullOrWhiteSpace($PointerKey)) {
    "agent-$agentSlug.json"
  }
  else {
    "agent-$agentSlug--$(ConvertTo-Slug -Text $PointerKey).json"
  }

  return (Join-Path (Get-ProjectStreamReadPointersPath -Root $Root -ProjectId $ProjectId) $fileName)
}

function Get-ProjectOperationLogsPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectLogsPath -Root $Root -ProjectId $ProjectId) "operations")
}

function Get-ProjectConflictLogsPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectLogsPath -Root $Root -ProjectId $ProjectId) "conflicts")
}

function Get-ProjectSessionsPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "SESSIONS")
}

function Get-ProjectActiveSessionsPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectSessionsPath -Root $Root -ProjectId $ProjectId) "active")
}

function Get-ProjectArchivedSessionsPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectSessionsPath -Root $Root -ProjectId $ProjectId) "archive")
}

function Get-ProjectStagingPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "STAGING")
}

function Get-ProjectLiveStateBoardFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectContextPath -Root $Root -ProjectId $ProjectId) "LIVE_STATE_BOARD.md")
}

function Get-ProjectConflictMarkdownFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectLogsPath -Root $Root -ProjectId $ProjectId) "CONFLICT_LOG.md")
}

function Get-ProjectPatchStagingPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectStagingPath -Root $Root -ProjectId $ProjectId) "patches")
}

function Get-ProjectTasksPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "tasks")
}

function Get-ProjectLocksPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "locks")
}

function Get-ProjectActivityPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "activity")
}

function Get-ProjectStatePath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "state")
}

function Get-ProjectInternalLocksPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectStatePath -Root $Root -ProjectId $ProjectId) "internal_locks")
}

function Get-ProjectNamedLockFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$LockName
  )

  return (Join-Path (Get-ProjectInternalLocksPath -Root $Root -ProjectId $ProjectId) ($LockName + ".lock"))
}

function Get-ProjectEventsPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectStatePath -Root $Root -ProjectId $ProjectId) "events")
}

function Get-ProjectStateEventLogFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectEventsPath -Root $Root -ProjectId $ProjectId) "STATE_EVENTS.jsonl")
}

function Get-ProjectTransactionsPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectStatePath -Root $Root -ProjectId $ProjectId) "transactions")
}

function Get-ProjectActiveTransactionsPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectTransactionsPath -Root $Root -ProjectId $ProjectId) "active")
}

function Get-ProjectTransactionHistoryPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectTransactionsPath -Root $Root -ProjectId $ProjectId) "history")
}

function Get-ProjectTransactionFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$TransactionId,
    [ValidateSet("active", "history")]
    [string]$State = "active"
  )

  $basePath = if ($State -eq "history") {
    Get-ProjectTransactionHistoryPath -Root $Root -ProjectId $ProjectId
  }
  else {
    Get-ProjectActiveTransactionsPath -Root $Root -ProjectId $ProjectId
  }

  return (Join-Path $basePath ($TransactionId + ".json"))
}

function Get-ProjectBaselineStateFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectChangelogLevelPath -Root $Root -ProjectId $ProjectId -Level "L0_BASE") "BASELINE_STATE.json")
}

function Get-ProjectDigestPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectChangelogPath -Root $Root -ProjectId $ProjectId) "L3_DIGEST")
}

function Get-ProjectDigestIndexFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectDigestPath -Root $Root -ProjectId $ProjectId) "DIGEST_INDEX.json")
}

function Get-ProjectDigestLatestSummaryFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectDigestPath -Root $Root -ProjectId $ProjectId) "latest_summary.md")
}

function Get-ProjectDigestFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId
  )

  return (Join-Path (Get-ProjectDigestPath -Root $Root -ProjectId $ProjectId) ("digest_" + $SessionId + ".md"))
}

function Get-ProjectActiveSessionsIndexFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectChangelogLevelPath -Root $Root -ProjectId $ProjectId -Level "L1_ACTIVE") "ACTIVE_SESSIONS.json")
}

function Get-ProjectL1EntryFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId
  )

  return (Join-Path (Get-ProjectChangelogLevelPath -Root $Root -ProjectId $ProjectId -Level "L1_ACTIVE") ($SessionId + ".json"))
}

function Get-ProjectRecentIndexFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectChangelogLevelPath -Root $Root -ProjectId $ProjectId -Level "L2_RECENT") "RECENT_INDEX.json")
}

function Get-ProjectRecentBucketPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$DatePartition
  )

  return (Join-Path (Get-ProjectChangelogLevelPath -Root $Root -ProjectId $ProjectId -Level "L2_RECENT") $DatePartition)
}

function Get-ProjectRecentEntryFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [Parameter(Mandatory = $true)]
    [string]$DatePartition
  )

  return (Join-Path (Get-ProjectRecentBucketPath -Root $Root -ProjectId $ProjectId -DatePartition $DatePartition) ($SessionId + ".json"))
}

function Get-ProjectArchiveIndexFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectChangelogLevelPath -Root $Root -ProjectId $ProjectId -Level "L4_ARCHIVE") "ARCHIVE_INDEX.json")
}

function Get-ProjectArchiveBucketPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$MonthPartition
  )

  return (Join-Path (Get-ProjectChangelogLevelPath -Root $Root -ProjectId $ProjectId -Level "L4_ARCHIVE") $MonthPartition)
}

function Get-ProjectArchiveEntryFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [Parameter(Mandatory = $true)]
    [string]$MonthPartition
  )

  return (Join-Path (Get-ProjectArchiveBucketPath -Root $Root -ProjectId $ProjectId -MonthPartition $MonthPartition) ($SessionId + ".json"))
}

function Invoke-WithProjectLock {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$LockName,
    [Parameter(Mandatory = $true)]
    [scriptblock]$ScriptBlock,
    [int]$TimeoutMs = 30000
  )

  $projectRuntimeLockPath = Get-ProjectNamedLockFile -Root $Root -ProjectId $ProjectId -LockName $LockName
  return Invoke-WithFileLock -LockPath $projectRuntimeLockPath -TimeoutMs $TimeoutMs -ScriptBlock $ScriptBlock
}

function New-ProjectTransactionId {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ChainName
  )

  return ("TX-{0}-{1}-{2}" -f (ConvertTo-Slug -Text $ChainName), (Get-Date -Format "yyyyMMdd-HHmmss"), [guid]::NewGuid().ToString("N").Substring(0, 8))
}

function Append-ProjectStateEvent {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$EventType,
    [string]$ChainName,
    [string]$TransactionId,
    [string]$EntityType,
    [string]$EntityId,
    [hashtable]$Payload
  )

  $now = Get-NowIso
  $record = @{
    event_id = "EV-{0}-{1}" -f (Get-Date -Format "yyyyMMddHHmmssfff"), [guid]::NewGuid().ToString("N").Substring(0, 6)
    project_id = $ProjectId
    chain = $ChainName
    transaction_id = $TransactionId
    event_type = $EventType
    entity_type = $EntityType
    entity_id = $EntityId
    timestamp = $now
  }
  if ($null -ne $Payload -and $Payload.Count -gt 0) {
    $record["payload"] = $Payload
  }

  Append-JsonLine -Path (Get-ProjectStateEventLogFile -Root $Root -ProjectId $ProjectId) -Object $record
  return $record
}

function Start-ProjectTransaction {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$ChainName,
    [hashtable]$Metadata
  )

  $txId = New-ProjectTransactionId -ChainName $ChainName
  $now = Get-NowIso
  $transaction = @{
    version = 1
    transactionId = $txId
    chain = $ChainName
    projectId = $ProjectId
    status = "preparing"
    phase = "prepare"
    createdAt = $now
    updatedAt = $now
    metadata = if ($null -ne $Metadata) { $Metadata } else { @{} }
  }

  $activePath = Get-ProjectTransactionFile -Root $Root -ProjectId $ProjectId -TransactionId $txId -State "active"
  Write-JsonFile -Path $activePath -Object $transaction
  Append-ProjectStateEvent -Root $Root -ProjectId $ProjectId -EventType "transaction_prepare" -ChainName $ChainName -TransactionId $txId -EntityType "transaction" -EntityId $txId -Payload @{
    status = "preparing"
    metadata = $transaction["metadata"]
  } | Out-Null
  return $transaction
}

function Update-ProjectTransaction {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$TransactionId,
    [string]$Status,
    [string]$Phase,
    [string]$Message,
    [hashtable]$Metadata
  )

  $path = Get-ProjectTransactionFile -Root $Root -ProjectId $ProjectId -TransactionId $TransactionId -State "active"
  $transaction = if (Test-Path -LiteralPath $path) { Read-JsonFile -Path $path } else { @{} }
  if (-not ($transaction -is [hashtable])) {
    $transaction = @{}
  }

  $transaction["version"] = 1
  $transaction["transactionId"] = $TransactionId
  $transaction["projectId"] = $ProjectId
  if (-not $transaction.ContainsKey("chain")) {
    $transaction["chain"] = "unknown"
  }
  if (-not [string]::IsNullOrWhiteSpace($Status)) {
    $transaction["status"] = $Status
  }
  if (-not [string]::IsNullOrWhiteSpace($Phase)) {
    $transaction["phase"] = $Phase
  }
  if (-not [string]::IsNullOrWhiteSpace($Message)) {
    $transaction["message"] = $Message
  }
  if ($null -ne $Metadata -and $Metadata.Count -gt 0) {
    if (-not $transaction.ContainsKey("metadata") -or -not ($transaction["metadata"] -is [hashtable])) {
      $transaction["metadata"] = @{}
    }
    foreach ($key in $Metadata.Keys) {
      $transaction["metadata"][$key] = $Metadata[$key]
    }
  }
  $transaction["updatedAt"] = Get-NowIso
  Write-JsonFile -Path $path -Object $transaction
  return $transaction
}

function Complete-ProjectTransaction {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$TransactionId,
    [string]$Message,
    [hashtable]$Metadata
  )

  $transaction = Update-ProjectTransaction -Root $Root -ProjectId $ProjectId -TransactionId $TransactionId -Status "committed" -Phase "commit" -Message $Message -Metadata $Metadata
  $transaction["completedAt"] = Get-NowIso
  $historyPath = Get-ProjectTransactionFile -Root $Root -ProjectId $ProjectId -TransactionId $TransactionId -State "history"
  $activePath = Get-ProjectTransactionFile -Root $Root -ProjectId $ProjectId -TransactionId $TransactionId -State "active"
  Write-JsonFile -Path $historyPath -Object $transaction
  if (Test-Path -LiteralPath $activePath) {
    Remove-Item -LiteralPath $activePath -Force -ErrorAction SilentlyContinue
  }
  Append-ProjectStateEvent -Root $Root -ProjectId $ProjectId -EventType "transaction_commit" -ChainName ([string]$transaction["chain"]) -TransactionId $TransactionId -EntityType "transaction" -EntityId $TransactionId -Payload @{
    status = "committed"
    message = $Message
  } | Out-Null
  return $transaction
}

function Fail-ProjectTransaction {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$TransactionId,
    [Parameter(Mandatory = $true)]
    [string]$Message,
    [hashtable]$Metadata,
    [switch]$RolledBack
  )

  $statusValue = if ($RolledBack) { "rolled_back" } else { "failed" }
  $phaseValue = if ($RolledBack) { "rollback" } else { "error" }
  $transaction = Update-ProjectTransaction -Root $Root -ProjectId $ProjectId -TransactionId $TransactionId -Status $statusValue -Phase $phaseValue -Message $Message -Metadata $Metadata
  $transaction["completedAt"] = Get-NowIso
  $historyPath = Get-ProjectTransactionFile -Root $Root -ProjectId $ProjectId -TransactionId $TransactionId -State "history"
  $activePath = Get-ProjectTransactionFile -Root $Root -ProjectId $ProjectId -TransactionId $TransactionId -State "active"
  Write-JsonFile -Path $historyPath -Object $transaction
  if (Test-Path -LiteralPath $activePath) {
    Remove-Item -LiteralPath $activePath -Force -ErrorAction SilentlyContinue
  }
  Append-ProjectStateEvent -Root $Root -ProjectId $ProjectId -EventType ("transaction_" + $statusValue) -ChainName ([string]$transaction["chain"]) -TransactionId $TransactionId -EntityType "transaction" -EntityId $TransactionId -Payload @{
    status = $statusValue
    message = $Message
  } | Out-Null
  return $transaction
}

function Get-ReadModeSignature {
  param(
    [string[]]$Types = @(),
    [switch]$OtherAgentsOnly
  )

  $normalizedTypes = @()
  foreach ($typeValue in (ConvertTo-StringArray -Value $Types)) {
    switch ($typeValue.Trim().ToUpperInvariant()) {
      "THOUGHT" { $normalizedTypes += "T" }
      "EXEC" { $normalizedTypes += "E" }
      "MOD" { $normalizedTypes += "M" }
      "T" { $normalizedTypes += "T" }
      "E" { $normalizedTypes += "E" }
      "M" { $normalizedTypes += "M" }
      default { }
    }
  }
  $normalizedTypes = @($normalizedTypes | Sort-Object -Unique)
  $typePart = if (@($normalizedTypes).Count -gt 0) { [string]::Join("", $normalizedTypes) } else { "ALL" }
  $scopePart = if ($OtherAgentsOnly) { "OTHER" } else { "ANY" }
  return ("mode-" + $scopePart + "-" + $typePart)
}

function Get-ProjectSessionDirectory {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [ValidateSet("active", "archive")]
    [string]$State = "active",
    [string]$Partition
  )

  if ($State -eq "active") {
    return (Join-Path (Get-ProjectActiveSessionsPath -Root $Root -ProjectId $ProjectId) $SessionId)
  }

  $archiveRoot = Get-ProjectArchivedSessionsPath -Root $Root -ProjectId $ProjectId
  if (-not [string]::IsNullOrWhiteSpace($Partition)) {
    return (Join-Path (Join-Path $archiveRoot $Partition) $SessionId)
  }

  return (Join-Path $archiveRoot $SessionId)
}

function Get-ProjectSessionDeltaFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [ValidateSet("active", "archive")]
    [string]$State = "active",
    [string]$Partition
  )

  return (Join-Path (Get-ProjectSessionDirectory -Root $Root -ProjectId $ProjectId -SessionId $SessionId -State $State -Partition $Partition) "SESSION_DELTA.json")
}

function Get-ProjectSessionPromptFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [ValidateSet("active", "archive")]
    [string]$State = "active",
    [string]$Partition
  )

  return (Join-Path (Get-ProjectSessionDirectory -Root $Root -ProjectId $ProjectId -SessionId $SessionId -State $State -Partition $Partition) "AI_PROMPT.md")
}

function Get-ProjectSessionTriadFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [Parameter(Mandatory = $true)]
    [ValidateSet("THOUGHT", "EXEC", "MOD")]
    [string]$TriadType,
    [ValidateSet("active", "archive")]
    [string]$State = "active",
    [string]$Partition
  )

  return (Join-Path (Get-ProjectSessionDirectory -Root $Root -ProjectId $ProjectId -SessionId $SessionId -State $State -Partition $Partition) ($TriadType + ".jsonl"))
}

function Get-ProjectOperationLogFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [Parameter(Mandatory = $true)]
    [string]$DatePartition
  )

  return (Join-Path (Join-Path (Get-ProjectOperationLogsPath -Root $Root -ProjectId $ProjectId) $DatePartition) ($SessionId + ".jsonl"))
}

function Get-ProjectConflictLogFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [Parameter(Mandatory = $true)]
    [string]$DatePartition
  )

  return (Join-Path (Join-Path (Get-ProjectConflictLogsPath -Root $Root -ProjectId $ProjectId) $DatePartition) ($SessionId + ".jsonl"))
}

function Get-ProjectWorktreeRoot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-CoordinationWorktreesRoot -Root $Root) $ProjectId)
}

function Get-ProjectFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "project.json")
}

function Get-ProjectRegistryFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectStatePath -Root $Root -ProjectId $ProjectId) "registry.json")
}

function Get-ProjectOnboardingPromptFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectContextPath -Root $Root -ProjectId $ProjectId) "AI_ONBOARDING_PROMPT.md")
}

function Get-ProjectAiStartFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  return (Join-Path (Get-ProjectCoordinationPath -Root $Root -ProjectId $ProjectId) "AI_START.md")
}

function Read-ProjectConfig {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  $path = Get-ProjectFile -Root $Root -ProjectId $ProjectId
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Project config not found: $path"
  }

  return Read-JsonFile -Path $path
}

function Read-ProjectRegistry {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  $path = Get-ProjectRegistryFile -Root $Root -ProjectId $ProjectId
  $project = $null
  $projectFile = Get-ProjectFile -Root $Root -ProjectId $ProjectId
  if (Test-Path -LiteralPath $projectFile) {
    $project = Read-JsonFile -Path $projectFile
  }

  if (-not (Test-Path -LiteralPath $path)) {
    return Normalize-ProjectRegistry -Registry @{} -ProjectId $ProjectId -Project $project
  }

  return Normalize-ProjectRegistry -Registry (Read-JsonFile -Path $path) -ProjectId $ProjectId -Project $project
}

function New-SessionId {
  param(
    [Parameter(Mandatory = $true)]
    [string]$AgentName,
    [Parameter(Mandatory = $true)]
    [string]$TaskId
  )

  return ("SESSION-{0}-{1}-{2}" -f (Get-Date -Format "yyyyMMdd-HHmmss"), (ConvertTo-Slug -Text $AgentName), (ConvertTo-Slug -Text $TaskId))
}

function Append-JsonLine {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    $Object
  )

  $parent = Split-Path -Parent $Path
  if (-not [string]::IsNullOrWhiteSpace($parent)) {
    Ensure-Directory -Path $parent
  }

  $json = ($Object | ConvertTo-Json -Depth 16 -Compress) + [Environment]::NewLine
  $encoding = New-Object System.Text.UTF8Encoding($false)
  Invoke-WithFileLock -LockPath (Get-SidecarLockPath -Path $Path) -ScriptBlock {
    [System.IO.File]::AppendAllText($Path, $json, $encoding)
  }
}

function Read-JsonLineObjects {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [string]$AfterEntryId,
    [int]$MaxItems = 0
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return @()
  }

  $records = @()
  $started = [string]::IsNullOrWhiteSpace($AfterEntryId)
  foreach ($line in (Get-Content -LiteralPath $Path -ErrorAction SilentlyContinue)) {
    if ([string]::IsNullOrWhiteSpace($line)) {
      continue
    }

    $parsed = ConvertFrom-JsonSafe -Text $line
    if ($null -eq $parsed) {
      continue
    }

    if (-not $started) {
      if ($parsed.ContainsKey("entry_id") -and ([string]$parsed["entry_id"] -eq $AfterEntryId)) {
        $started = $true
      }
      continue
    }

    $records += ,$parsed
    if (($MaxItems -gt 0) -and (@($records).Count -ge $MaxItems)) {
      break
    }
  }

  return $records
}

function Upsert-ListEntry {
  param(
    [array]$Items,
    [hashtable]$Item,
    [Parameter(Mandatory = $true)]
    [string]$Key
  )

  $result = @()
  $found = $false
  foreach ($existing in ($Items | ForEach-Object { $_ })) {
    if ($existing[$Key] -eq $Item[$Key]) {
      $result += ,$Item
      $found = $true
    }
    else {
      $result += ,$existing
    }
  }

  if (-not $found) {
    $result += ,$Item
  }

  return ,@($result)
}

function Remove-ListEntry {
  param(
    [array]$Items,
    [Parameter(Mandatory = $true)]
    [string]$Key,
    [Parameter(Mandatory = $true)]
    [string]$Value
  )

  $result = @()
  foreach ($existing in ($Items | ForEach-Object { $_ })) {
    if ($existing[$Key] -ne $Value) {
      $result += ,$existing
    }
  }

  return ,@($result)
}

function ConvertTo-StringArray {
  param(
    $Value
  )

  if ($null -eq $Value) {
    return ,([string[]]@())
  }

  if ($Value -is [string]) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
      return ,([string[]]@())
    }
    return ,([string[]]@([string]$Value))
  }

  if (($Value -is [System.Collections.IEnumerable]) -and -not ($Value -is [string])) {
    $items = @()
    foreach ($item in $Value) {
      if ($null -eq $item) {
        continue
      }
      $text = [string]$item
      if (-not [string]::IsNullOrWhiteSpace($text)) {
        $items += $text
      }
    }
    return ,([string[]]$items)
  }

  return ,([string[]]@([string]$Value))
}

function Normalize-RepoRelativePath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  $repoFull = [System.IO.Path]::GetFullPath($RepoRoot)
  $targetFull = if ([System.IO.Path]::IsPathRooted($Path)) {
    [System.IO.Path]::GetFullPath($Path)
  }
  else {
    [System.IO.Path]::GetFullPath((Join-Path $repoFull $Path))
  }

  $normalizedRepo = $repoFull.TrimEnd("\", "/")
  if ($targetFull.StartsWith($normalizedRepo, [System.StringComparison]::OrdinalIgnoreCase)) {
    $relative = $targetFull.Substring($normalizedRepo.Length).TrimStart("\", "/")
    return ($relative -replace "\\", "/")
  }

  return ($Path -replace "\\", "/")
}

function Test-PathCoveredByScope {
  param(
    [Parameter(Mandatory = $true)]
    [string]$TargetPath,
    [string[]]$Scope = @()
  )

  $normalizedTarget = ($TargetPath -replace "\\", "/").Trim("/")
  foreach ($entry in $Scope) {
    $normalizedScope = (($entry | ForEach-Object { $_ }) -join "") -replace "\\", "/"
    $normalizedScope = $normalizedScope.Trim("/")
    if ([string]::IsNullOrWhiteSpace($normalizedScope)) {
      continue
    }

    if ($normalizedTarget -eq $normalizedScope -or $normalizedTarget.StartsWith($normalizedScope + "/")) {
      return $true
    }
  }

  return $false
}

function Test-ScopeOverlap {
  param(
    [string[]]$Left = @(),
    [string[]]$Right = @()
  )

  foreach ($leftEntry in $Left) {
    foreach ($rightEntry in $Right) {
      if ((Test-PathCoveredByScope -TargetPath $leftEntry -Scope @($rightEntry)) -or (Test-PathCoveredByScope -TargetPath $rightEntry -Scope @($leftEntry))) {
        return $true
      }
    }
  }

  return $false
}

function Get-RecentSessionRetentionHours {
  return 72
}

function Get-RecentSessionRetentionCount {
  return 20
}

function Get-StreamTailSize {
  return 25
}

function Get-DigestSessionRetentionCount {
  return 8
}

function Get-DigestWordLimit {
  return 220
}

function Get-TriadTypeShortCode {
  param(
    [Parameter(Mandatory = $true)]
    [string]$TriadType
  )

  switch ($TriadType.ToUpperInvariant()) {
    "THOUGHT" { return "T" }
    "EXEC" { return "E" }
    "MOD" { return "M" }
    "T" { return "T" }
    "E" { return "E" }
    "M" { return "M" }
    default { throw "Unsupported triad type: $TriadType" }
  }
}

function Get-TriadTypeName {
  param(
    [Parameter(Mandatory = $true)]
    [string]$TriadType
  )

  switch ((Get-TriadTypeShortCode -TriadType $TriadType)) {
    "T" { return "THOUGHT" }
    "E" { return "EXEC" }
    "M" { return "MOD" }
    default { throw "Unsupported triad type: $TriadType" }
  }
}

function New-Sha256Checksum {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Text
  )

  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    $hash = $sha.ComputeHash($bytes)
    return ([System.BitConverter]::ToString($hash)).Replace("-", "").ToLowerInvariant()
  }
  finally {
    $sha.Dispose()
  }
}

function New-TriadEntryId {
  param(
    [Parameter(Mandatory = $true)]
    [string]$TriadType
  )

  return ("ENTRY-{0}-{1}-{2}" -f (Get-Date -Format "yyyyMMdd-HHmmss-fffffff"), (Get-TriadTypeShortCode -TriadType $TriadType), ([guid]::NewGuid().ToString("N").Substring(0, 6)))
}

function Resolve-ProjectSessionDeltaPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId
  )

  $activePath = Get-ProjectSessionDeltaFile -Root $Root -ProjectId $ProjectId -SessionId $SessionId -State "active"
  if (Test-Path -LiteralPath $activePath) {
    return $activePath
  }

  $archiveRoot = Get-ProjectArchivedSessionsPath -Root $Root -ProjectId $ProjectId
  if (Test-Path -LiteralPath $archiveRoot) {
    $match = Get-ChildItem -LiteralPath $archiveRoot -Recurse -Filter "SESSION_DELTA.json" -ErrorAction SilentlyContinue | Where-Object {
      (Split-Path -Parent $_.FullName | Split-Path -Leaf) -eq $SessionId
    } | Select-Object -First 1
    if ($match) {
      return $match.FullName
    }
  }

  return $null
}

function Resolve-ProjectSessionLocation {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId
  )

  $activeDir = Get-ProjectSessionDirectory -Root $Root -ProjectId $ProjectId -SessionId $SessionId -State "active"
  if (Test-Path -LiteralPath $activeDir) {
    return @{
      state = "active"
      sessionDir = $activeDir
      partition = $null
      sessionDeltaFile = Get-ProjectSessionDeltaFile -Root $Root -ProjectId $ProjectId -SessionId $SessionId -State "active"
    }
  }

  $archiveRoot = Get-ProjectArchivedSessionsPath -Root $Root -ProjectId $ProjectId
  if (Test-Path -LiteralPath $archiveRoot) {
    $match = Get-ChildItem -LiteralPath $archiveRoot -Recurse -Filter "SESSION_DELTA.json" -ErrorAction SilentlyContinue | Where-Object {
      (Split-Path -Parent $_.FullName | Split-Path -Leaf) -eq $SessionId
    } | Select-Object -First 1
    if ($match) {
      $sessionDir = Split-Path -Parent $match.FullName
      $partitionDir = Split-Path -Parent $sessionDir
      $partition = Split-Path -Leaf $partitionDir
      return @{
        state = "archive"
        sessionDir = $sessionDir
        partition = $partition
        sessionDeltaFile = $match.FullName
      }
    }
  }

  return $null
}

function Ensure-SessionTriadFiles {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [ValidateSet("active", "archive")]
    [string]$State = "active",
    [string]$Partition
  )

  foreach ($triadType in @("THOUGHT", "EXEC", "MOD")) {
    $path = Get-ProjectSessionTriadFile -Root $Root -ProjectId $ProjectId -SessionId $SessionId -TriadType $triadType -State $State -Partition $Partition
    if (-not (Test-Path -LiteralPath $path)) {
      Write-TextFile -Path $path -Content ""
    }
  }
}

function Read-ProjectReadPointer {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$AgentId,
    [string]$PointerKey
  )

  $path = Get-ProjectReadPointerFile -Root $Root -ProjectId $ProjectId -AgentId $AgentId -PointerKey $PointerKey
  if (-not (Test-Path -LiteralPath $path)) {
    return @{
      version = 1
      projectId = $ProjectId
      agentId = $AgentId
      pointerKey = if ([string]::IsNullOrWhiteSpace($PointerKey)) { $null } else { $PointerKey }
      lastEntryId = $null
      lastTimestamp = $null
      updatedAt = Get-NowIso
    }
  }

  return Read-JsonFile -Path $path
}

function Write-ProjectReadPointer {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$AgentId,
    [string]$PointerKey,
    [Parameter(Mandatory = $true)]
    [hashtable]$Pointer
  )

  $Pointer["version"] = 1
  $Pointer["projectId"] = $ProjectId
  $Pointer["agentId"] = $AgentId
  $Pointer["pointerKey"] = if ([string]::IsNullOrWhiteSpace($PointerKey)) { $null } else { $PointerKey }
  $Pointer["updatedAt"] = Get-NowIso
  Write-JsonFile -Path (Get-ProjectReadPointerFile -Root $Root -ProjectId $ProjectId -AgentId $AgentId -PointerKey $PointerKey) -Object $Pointer
}

function Read-LatestJsonLineObject {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }

  $line = Get-Content -LiteralPath $Path -Tail 1 -ErrorAction SilentlyContinue
  if ([string]::IsNullOrWhiteSpace([string]$line)) {
    return $null
  }

  return ConvertFrom-JsonSafe -Text ([string]$line)
}

function Read-LastJsonLineObjects {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [int]$Tail = 25
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    return @()
  }

  $records = @()
  foreach ($line in (Get-Content -LiteralPath $Path -Tail $Tail -ErrorAction SilentlyContinue)) {
    if ([string]::IsNullOrWhiteSpace([string]$line)) {
      continue
    }
    $parsed = ConvertFrom-JsonSafe -Text ([string]$line)
    if ($null -ne $parsed) {
      $records += ,$parsed
    }
  }
  return $records
}

function Refresh-ProjectStreamSnapshots {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  $streamFile = Get-ProjectStreamEntriesFile -Root $Root -ProjectId $ProjectId
  $tailFile = Get-ProjectStreamTailFile -Root $Root -ProjectId $ProjectId
  $newestFile = Get-ProjectStreamNewestEntryFile -Root $Root -ProjectId $ProjectId
  $timestamp = Get-NowIso
  $entries = Read-LastJsonLineObjects -Path $streamFile -Tail (Get-StreamTailSize)
  $entries = @($entries | Sort-Object {
      if ($_.ContainsKey("timestamp")) { [DateTimeOffset]::Parse([string]$_["timestamp"]) } else { [DateTimeOffset]::MinValue }
    })

  Write-JsonFile -Path $tailFile -Object @{
    version = 2
    updatedAt = $timestamp
    source = $streamFile
    maxEntries = Get-StreamTailSize
    entries = $entries
  }

  $newestEntry = if (@($entries).Count -gt 0) { $entries[-1] } else { $null }
  if ($null -eq $newestEntry) {
    Write-JsonFile -Path $newestFile -Object @{
      version = 2
      updatedAt = $timestamp
      source = $streamFile
      entry_id = $null
      session_id = $null
      agent_id = $null
      type = $null
      status = $null
      content_preview = ""
      related_files = @()
    }
    return
  }

  Write-JsonFile -Path $newestFile -Object @{
    version = 2
    updatedAt = $timestamp
    source = $streamFile
    entry_id = if ($newestEntry.ContainsKey("entry_id")) { [string]$newestEntry["entry_id"] } else { $null }
    project_id = if ($newestEntry.ContainsKey("project_id")) { [string]$newestEntry["project_id"] } else { $ProjectId }
    task_id = if ($newestEntry.ContainsKey("task_id")) { [string]$newestEntry["task_id"] } else { $null }
    session_id = if ($newestEntry.ContainsKey("session_id")) { [string]$newestEntry["session_id"] } else { $null }
    agent_id = if ($newestEntry.ContainsKey("agent_id")) { [string]$newestEntry["agent_id"] } else { $null }
    type = if ($newestEntry.ContainsKey("type")) { [string]$newestEntry["type"] } else { $null }
    type_name = if ($newestEntry.ContainsKey("type_name")) { [string]$newestEntry["type_name"] } else { $null }
    status = if ($newestEntry.ContainsKey("status")) { [string]$newestEntry["status"] } else { $null }
    triad_file = if ($newestEntry.ContainsKey("triad_file")) { [string]$newestEntry["triad_file"] } else { $null }
    content_preview = if ($newestEntry.ContainsKey("content")) { Get-TextPreview -Text ([string]$newestEntry["content"]) -MaxLength 160 } else { "" }
    related_files = if ($newestEntry.ContainsKey("related_files")) { ConvertTo-StringArray -Value $newestEntry["related_files"] } else { @() }
  }
}

function Update-ProjectSessionHeads {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  $projectFile = Get-ProjectFile -Root $Root -ProjectId $ProjectId
  if (-not (Test-Path -LiteralPath $projectFile)) {
    return
  }

  $registry = Read-ProjectRegistry -Root $Root -ProjectId $ProjectId
  $activeIndexFile = Get-ProjectActiveSessionsIndexFile -Root $Root -ProjectId $ProjectId
  $activeIndex = if (Test-Path -LiteralPath $activeIndexFile) { Read-JsonFile -Path $activeIndexFile } else { @{ sessions = @() } }
  $activeSessions = if ($activeIndex.ContainsKey("sessions")) { ConvertTo-ObjectArray -Value $activeIndex["sessions"] } else { @() }
  $taskEntries = ConvertTo-ObjectArray -Value $registry["tasks"]
  $lockEntries = ConvertTo-ObjectArray -Value $registry["locks"]
  $heads = @()

  foreach ($session in ($activeSessions | Sort-Object {
        if ($_.ContainsKey("updatedAt") -and -not [string]::IsNullOrWhiteSpace([string]($_["updatedAt"]))) {
          [DateTimeOffset]::Parse([string]($_["updatedAt"]))
        }
        else {
          [DateTimeOffset]::MinValue
        }
      } -Descending)) {
    if (-not ($session -is [hashtable])) {
      continue
    }

    $sessionId = if ($session.ContainsKey("sessionId")) { [string]$session["sessionId"] } else { "" }
    if ([string]::IsNullOrWhiteSpace($sessionId)) {
      continue
    }

    $taskId = if ($session.ContainsKey("taskId")) { [string]$session["taskId"] } else { "" }
    $agentId = if ($session.ContainsKey("agentId")) { [string]$session["agentId"] } else { "" }
    $sessionDeltaFile = if ($session.ContainsKey("sessionDeltaFile")) { [string]$session["sessionDeltaFile"] } else { Resolve-ProjectSessionDeltaPath -Root $Root -ProjectId $ProjectId -SessionId $sessionId }
    $sessionDelta = if (-not [string]::IsNullOrWhiteSpace($sessionDeltaFile) -and (Test-Path -LiteralPath $sessionDeltaFile)) { Read-JsonFile -Path $sessionDeltaFile } else { @{} }

    $taskEntry = $null
    foreach ($entry in $taskEntries) {
      if (($entry -is [hashtable]) -and $entry.ContainsKey("taskId") -and ([string]$entry["taskId"] -eq $taskId)) {
        $taskEntry = $entry
        break
      }
    }

    $lockEntry = $null
    foreach ($entry in $lockEntries) {
      if (($entry -is [hashtable]) -and $entry.ContainsKey("taskId") -and ([string]$entry["taskId"] -eq $taskId) -and $entry.ContainsKey("status") -and ([string]$entry["status"]).ToLowerInvariant() -eq "active") {
        $lockEntry = $entry
        break
      }
    }

    $location = Resolve-ProjectSessionLocation -Root $Root -ProjectId $ProjectId -SessionId $sessionId
    $thoughtEntry = $null
    $execEntry = $null
    $modEntry = $null
    if ($null -ne $location) {
      $thoughtEntry = Read-LatestJsonLineObject -Path (Get-ProjectSessionTriadFile -Root $Root -ProjectId $ProjectId -SessionId $sessionId -TriadType "THOUGHT" -State $location["state"] -Partition $location["partition"])
      $execEntry = Read-LatestJsonLineObject -Path (Get-ProjectSessionTriadFile -Root $Root -ProjectId $ProjectId -SessionId $sessionId -TriadType "EXEC" -State $location["state"] -Partition $location["partition"])
      $modEntry = Read-LatestJsonLineObject -Path (Get-ProjectSessionTriadFile -Root $Root -ProjectId $ProjectId -SessionId $sessionId -TriadType "MOD" -State $location["state"] -Partition $location["partition"])
    }

    $delta = if ($sessionDelta.ContainsKey("delta") -and ($sessionDelta["delta"] -is [hashtable])) { $sessionDelta["delta"] } else { @{} }
    $triadCounters = if ($sessionDelta.ContainsKey("triadCounters") -and ($sessionDelta["triadCounters"] -is [hashtable])) { $sessionDelta["triadCounters"] } else { @{ thought = 0; exec = 0; mod = 0 } }
    $scope = if ($null -ne $lockEntry -and $lockEntry.ContainsKey("scope")) { ConvertTo-StringArray -Value $lockEntry["scope"] } else { @() }
    $pendingWrites = if ($delta.ContainsKey("pendingWrites")) { ConvertTo-StringArray -Value $delta["pendingWrites"] } else { @() }
    $touchedFiles = if ($delta.ContainsKey("touchedFiles")) { ConvertTo-StringArray -Value $delta["touchedFiles"] } else { @() }
    $conflicts = if ($delta.ContainsKey("conflicts")) { ConvertTo-ObjectArray -Value $delta["conflicts"] } else { @() }

    $heads += ,@{
      sessionId = $sessionId
      taskId = $taskId
      agentId = if (-not [string]::IsNullOrWhiteSpace($agentId)) { $agentId } elseif ($sessionDelta.ContainsKey("agentId")) { [string]$sessionDelta["agentId"] } else { "" }
      status = if ($sessionDelta.ContainsKey("status")) { [string]$sessionDelta["status"] } else { "active" }
      updatedAt = if ($sessionDelta.ContainsKey("updatedAt")) { [string]$sessionDelta["updatedAt"] } elseif ($session.ContainsKey("updatedAt")) { [string]$session["updatedAt"] } else { Get-NowIso }
      branch = if ($null -ne $taskEntry -and $taskEntry.ContainsKey("branch")) { [string]$taskEntry["branch"] } elseif ($null -ne $lockEntry -and $lockEntry.ContainsKey("branch")) { [string]$lockEntry["branch"] } else { "" }
      worktreePath = if ($null -ne $taskEntry -and $taskEntry.ContainsKey("worktreePath")) { [string]$taskEntry["worktreePath"] } elseif ($null -ne $lockEntry -and $lockEntry.ContainsKey("worktreePath")) { [string]$lockEntry["worktreePath"] } else { "" }
      scope = $scope
      summary = if ($delta.ContainsKey("summary")) { [string]$delta["summary"] } else { "" }
      touchedFiles = $touchedFiles
      pendingWrites = $pendingWrites
      conflictCount = @($conflicts).Count
      triadCounters = @{
        thought = if ($triadCounters.ContainsKey("thought")) { [int]$triadCounters["thought"] } else { 0 }
        exec = if ($triadCounters.ContainsKey("exec")) { [int]$triadCounters["exec"] } else { 0 }
        mod = if ($triadCounters.ContainsKey("mod")) { [int]$triadCounters["mod"] } else { 0 }
      }
      lastThought = if ($null -ne $thoughtEntry) {
        @{
          entryId = if ($thoughtEntry.ContainsKey("entry_id")) { [string]$thoughtEntry["entry_id"] } else { "" }
          timestamp = if ($thoughtEntry.ContainsKey("timestamp")) { [string]$thoughtEntry["timestamp"] } else { "" }
          status = if ($thoughtEntry.ContainsKey("status")) { [string]$thoughtEntry["status"] } else { "" }
          contentPreview = if ($thoughtEntry.ContainsKey("content")) { Get-TextPreview -Text ([string]$thoughtEntry["content"]) -MaxLength 120 } else { "" }
          relatedFiles = if ($thoughtEntry.ContainsKey("related_files")) { ConvertTo-StringArray -Value $thoughtEntry["related_files"] } else { @() }
        }
      } else { $null }
      lastExec = if ($null -ne $execEntry) {
        @{
          entryId = if ($execEntry.ContainsKey("entry_id")) { [string]$execEntry["entry_id"] } else { "" }
          timestamp = if ($execEntry.ContainsKey("timestamp")) { [string]$execEntry["timestamp"] } else { "" }
          status = if ($execEntry.ContainsKey("status")) { [string]$execEntry["status"] } else { "" }
          contentPreview = if ($execEntry.ContainsKey("content")) { Get-TextPreview -Text ([string]$execEntry["content"]) -MaxLength 120 } else { "" }
          relatedFiles = if ($execEntry.ContainsKey("related_files")) { ConvertTo-StringArray -Value $execEntry["related_files"] } else { @() }
        }
      } else { $null }
      lastMod = if ($null -ne $modEntry) {
        @{
          entryId = if ($modEntry.ContainsKey("entry_id")) { [string]$modEntry["entry_id"] } else { "" }
          timestamp = if ($modEntry.ContainsKey("timestamp")) { [string]$modEntry["timestamp"] } else { "" }
          status = if ($modEntry.ContainsKey("status")) { [string]$modEntry["status"] } else { "" }
          contentPreview = if ($modEntry.ContainsKey("content")) { Get-TextPreview -Text ([string]$modEntry["content"]) -MaxLength 120 } else { "" }
          relatedFiles = if ($modEntry.ContainsKey("related_files")) { ConvertTo-StringArray -Value $modEntry["related_files"] } else { @() }
        }
      } else { $null }
    }
  }

  Write-JsonFile -Path (Get-ProjectSessionHeadsFile -Root $Root -ProjectId $ProjectId) -Object @{
    version = 1
    updatedAt = Get-NowIso
    projectId = $ProjectId
    sessions = $heads
  }
}

function Append-ConflictMarkdown {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$Message
  )

  $path = Get-ProjectConflictMarkdownFile -Root $Root -ProjectId $ProjectId
  if (-not (Test-Path -LiteralPath $path)) {
    $initial = @(
      "---"
      "file_type: conflict_log"
      "write_mode: append_only"
      "last_updated: $(Get-NowIso)"
      "---"
      "# Conflict Log"
      ""
      "## Entries"
      ""
    )
    Write-TextFile -Path $path -Content ([string]::Join([Environment]::NewLine, $initial))
  }

  $content = Get-Content -LiteralPath $path -Raw
  $content = [regex]::Replace($content, '(?m)^last_updated:\s*.*$', "last_updated: $(Get-NowIso)")
  $entry = "- $(Get-Date -Format "yyyy-MM-dd HH:mm:ss") $Message"
  $content = $content.TrimEnd() + [Environment]::NewLine + $entry + [Environment]::NewLine
  Write-TextFile -Path $path -Content $content
}

function Update-LiveStateBoard {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  $projectFile = Get-ProjectFile -Root $Root -ProjectId $ProjectId
  if (-not (Test-Path -LiteralPath $projectFile)) {
    return
  }

  $project = Read-JsonFile -Path $projectFile
  $registry = Read-ProjectRegistry -Root $Root -ProjectId $ProjectId
  $activeIndexFile = Get-ProjectActiveSessionsIndexFile -Root $Root -ProjectId $ProjectId
  $activeIndex = if (Test-Path -LiteralPath $activeIndexFile) { Read-JsonFile -Path $activeIndexFile } else { @{ sessions = @() } }
  $activeSessions = if ($activeIndex.ContainsKey("sessions")) { ConvertTo-ObjectArray -Value $activeIndex["sessions"] } else { @() }
  $tailFile = Get-ProjectStreamTailFile -Root $Root -ProjectId $ProjectId
  $tailState = if (Test-Path -LiteralPath $tailFile) { Read-JsonFile -Path $tailFile } else { @{ entries = @() } }
  $tailEntries = if ($tailState.ContainsKey("entries")) { ConvertTo-ObjectArray -Value $tailState["entries"] } else { @() }
  $sessionHeadsFile = Get-ProjectSessionHeadsFile -Root $Root -ProjectId $ProjectId
  $sessionHeadsState = if (Test-Path -LiteralPath $sessionHeadsFile) { Read-JsonFile -Path $sessionHeadsFile } else { @{ sessions = @() } }
  $sessionHeads = if ($sessionHeadsState.ContainsKey("sessions")) { ConvertTo-ObjectArray -Value $sessionHeadsState["sessions"] } else { @() }
  $newestFile = Get-ProjectStreamNewestEntryFile -Root $Root -ProjectId $ProjectId
  $newest = if (Test-Path -LiteralPath $newestFile) { Read-JsonFile -Path $newestFile } else { @{} }
  $digestLatest = Get-ProjectDigestLatestSummaryFile -Root $Root -ProjectId $ProjectId
  $digestPreview = if (Test-Path -LiteralPath $digestLatest) {
    $digestLines = @(Get-Content -LiteralPath $digestLatest)
    if (($digestLines.Count -gt 0) -and ($digestLines[0] -eq "---")) {
      $closingFence = -1
      for ($index = 1; $index -lt $digestLines.Count; $index += 1) {
        if ($digestLines[$index] -eq "---") {
          $closingFence = $index
          break
        }
      }
      if ($closingFence -ge 0) {
        ($digestLines | Select-Object -Skip ($closingFence + 1) -First 10) -join [Environment]::NewLine
      }
      else {
        ($digestLines | Select-Object -First 10) -join [Environment]::NewLine
      }
    }
    else {
      ($digestLines | Select-Object -First 10) -join [Environment]::NewLine
    }
  }
  else {
    ""
  }

  $activeTaskLines = @()
  foreach ($task in ((ConvertTo-ObjectArray -Value $registry["tasks"]) | Where-Object {
        $_ -is [hashtable] -and $_.ContainsKey("status") -and ([string]($_["status"])).ToLowerInvariant() -eq "active"
      })) {
    $activeTaskLines += ('- `{0}` owner=`{1}` session=`{2}`' -f [string]$task["taskId"], [string]$task["owner"], [string]$task["sessionId"])
  }
  if (@($activeTaskLines).Count -eq 0) {
    $activeTaskLines = @("- No active tasks.")
  }

  $sessionHeadLines = @()
  foreach ($head in ($sessionHeads | Sort-Object {
        if ($_.ContainsKey("updatedAt") -and -not [string]::IsNullOrWhiteSpace([string]($_["updatedAt"]))) {
          [DateTimeOffset]::Parse([string]($_["updatedAt"]))
        }
        else {
          [DateTimeOffset]::MinValue
        }
      } -Descending)) {
    $scopeText = if ($head.ContainsKey("scope") -and @($head["scope"]).Count -gt 0) { [string]::Join(", ", @($head["scope"])) } else { "none" }
    $pendingCount = if ($head.ContainsKey("pendingWrites")) { @($head["pendingWrites"]).Count } else { 0 }
    $lastThoughtText = if ($head.ContainsKey("lastThought") -and $null -ne $head["lastThought"]) { [string]$head["lastThought"]["contentPreview"] } else { "none" }
    $lastExecText = if ($head.ContainsKey("lastExec") -and $null -ne $head["lastExec"]) { [string]$head["lastExec"]["contentPreview"] } else { "none" }
    $lastModText = if ($head.ContainsKey("lastMod") -and $null -ne $head["lastMod"]) { [string]$head["lastMod"]["contentPreview"] } else { "none" }
    $sessionHeadLines += ('- task=`{0}` agent=`{1}` session=`{2}` scope=`{3}` pending_writes={4} thought={5} exec={6} mod={7}' -f [string]$head["taskId"], [string]$head["agentId"], [string]$head["sessionId"], $scopeText, $pendingCount, $lastThoughtText, $lastExecText, $lastModText)
  }
  if (@($sessionHeadLines).Count -eq 0) {
    $sessionHeadLines = @("- No active session heads yet.")
  }

  $blockerLines = @()
  foreach ($entry in ($tailEntries | Sort-Object {
        if ($_.ContainsKey("timestamp")) { [DateTimeOffset]::Parse([string]($_["timestamp"])) } else { [DateTimeOffset]::MinValue }
      } -Descending | Select-Object -First 20)) {
    $statusValue = if ($entry.ContainsKey("status")) { [string]$entry["status"] } else { "" }
    if ($statusValue -in @("blocked", "failed", "conflict", "rejected")) {
      $blockerLines += ('- [{0}] `{1}` {2}' -f $statusValue, [string]$entry["type_name"], (Get-TextPreview -Text ([string]$entry["content"]) -MaxLength 120))
    }
  }
  if (@($blockerLines).Count -eq 0) {
    $blockerLines = @("- No active blockers recorded in the latest stream summary window.")
  }

  $decisionLines = @()
  foreach ($entry in ($tailEntries | Sort-Object {
        if ($_.ContainsKey("timestamp")) { [DateTimeOffset]::Parse([string]($_["timestamp"])) } else { [DateTimeOffset]::MinValue }
      } -Descending)) {
    if (($entry["type"] -eq "T") -or ($entry["type"] -eq "M")) {
      $decisionLines += ('- [{0}] `{1}` {2}' -f [string]$entry["type_name"], [string]$entry["agent_id"], (Get-TextPreview -Text ([string]$entry["content"]) -MaxLength 120))
    }
    if (@($decisionLines).Count -ge 6) {
      break
    }
  }
  if (@($decisionLines).Count -eq 0) {
    $decisionLines = @("- No key decisions recorded yet.")
  }

  $tailLines = @()
  foreach ($entry in ($tailEntries | Sort-Object {
        if ($_.ContainsKey("timestamp")) { [DateTimeOffset]::Parse([string]($_["timestamp"])) } else { [DateTimeOffset]::MinValue }
      } -Descending | Select-Object -First 10)) {
    $tailLines += ('- [{0}/{1}] `{2}` session=`{3}` {4}' -f [string]$entry["type"], [string]$entry["status"], [string]$entry["agent_id"], [string]$entry["session_id"], (Get-TextPreview -Text ([string]$entry["content"]) -MaxLength 120))
  }
  if (@($tailLines).Count -eq 0) {
    $tailLines = @("- Stream summary window is empty.")
  }

  $content = @(
    "---"
    "file_type: live_state_board"
    "project_id: $ProjectId"
    "write_mode: script_managed"
    "last_updated: $(Get-NowIso)"
    "---"
    "# Live State Board"
    ""
    "## Overview"
    ""
    ('- Project: `{0}`' -f [string]$project["name"])
    ('- Active sessions: {0}' -f @($activeSessions).Count)
    ('- Active tasks: {0}' -f @($activeTaskLines | Where-Object { $_ -notmatch '^-\sNo active tasks' }).Count)
    ('- Newest stream entry: `{0}`' -f $(if ($newest.ContainsKey("entry_id") -and -not [string]::IsNullOrWhiteSpace([string]$newest["entry_id"])) { [string]$newest["entry_id"] } else { "none" }))
    ('- Latest digest: `{0}`' -f (Get-ProjectDigestLatestSummaryFile -Root $Root -ProjectId $ProjectId))
    ""
    "## Active Tasks"
    ""
  ) + $activeTaskLines + @(
    ""
    "## Live Session Heads"
    ""
  ) + $sessionHeadLines + @(
    ""
    "## Blockers"
    ""
  ) + $blockerLines + @(
    ""
    "## Key Decisions"
    ""
  ) + $decisionLines + @(
    ""
    "## Latest Stream Tail"
    ""
  ) + $tailLines + @(
    ""
    "## Digest Preview"
    ""
  )

  if ([string]::IsNullOrWhiteSpace($digestPreview)) {
    $content += "- No digest summary exists yet."
  }
  else {
    $content += $digestPreview
  }

  Write-TextFile -Path (Get-ProjectLiveStateBoardFile -Root $Root -ProjectId $ProjectId) -Content ([string]::Join([Environment]::NewLine, $content))
}

function Write-TriadRecord {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [Parameter(Mandatory = $true)]
    [string]$AgentId,
    [Parameter(Mandatory = $true)]
    [string]$TriadType,
    [Parameter(Mandatory = $true)]
    [string]$Content,
    [string[]]$RelatedFiles = @(),
    [string]$Status = "completed",
    [string]$TaskId,
    [hashtable]$Metadata,
    [switch]$SkipProjectLock
  )

  if (-not $SkipProjectLock) {
    return Invoke-WithProjectLock -Root $Root -ProjectId $ProjectId -LockName "runtime" -ScriptBlock {
      Write-TriadRecord -Root $Root -ProjectId $ProjectId -SessionId $SessionId -AgentId $AgentId -TriadType $TriadType -Content $Content -RelatedFiles $RelatedFiles -Status $Status -TaskId $TaskId -Metadata $Metadata -SkipProjectLock
    }
  }

  $location = Resolve-ProjectSessionLocation -Root $Root -ProjectId $ProjectId -SessionId $SessionId
  if ($null -eq $location) {
    throw "Session location not found for triad record: $SessionId"
  }

  $project = Read-ProjectConfig -Root $Root -ProjectId $ProjectId
  $repoRoot = Resolve-GitRepositoryRoot -Path ([string]$project["repoPath"])
  $typeName = Get-TriadTypeName -TriadType $TriadType
  $typeCode = Get-TriadTypeShortCode -TriadType $TriadType
  $timestamp = Get-NowIso
  $entryId = New-TriadEntryId -TriadType $typeCode
  $normalizedFiles = @()
  foreach ($path in (ConvertTo-StringArray -Value $RelatedFiles)) {
    $normalizedFiles += (Normalize-RepoRelativePath -RepoRoot $repoRoot -Path $path)
  }
  $normalizedFiles = @($normalizedFiles | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)

  Ensure-SessionTriadFiles -Root $Root -ProjectId $ProjectId -SessionId $SessionId -State $location["state"] -Partition $location["partition"]
  $triadFile = Get-ProjectSessionTriadFile -Root $Root -ProjectId $ProjectId -SessionId $SessionId -TriadType $typeName -State $location["state"] -Partition $location["partition"]
  $projectStreamFile = Get-ProjectStreamEntriesFile -Root $Root -ProjectId $ProjectId
  $newestEntryFile = Get-ProjectStreamNewestEntryFile -Root $Root -ProjectId $ProjectId
  $tailFile = Get-ProjectStreamTailFile -Root $Root -ProjectId $ProjectId
  $readPointersPath = Get-ProjectStreamReadPointersPath -Root $Root -ProjectId $ProjectId
  $liveStateBoardFile = Get-ProjectLiveStateBoardFile -Root $Root -ProjectId $ProjectId
  $digestLatestFile = Get-ProjectDigestLatestSummaryFile -Root $Root -ProjectId $ProjectId
  $conflictMarkdownFile = Get-ProjectConflictMarkdownFile -Root $Root -ProjectId $ProjectId

  Ensure-Directory -Path (Get-ProjectStreamLogPath -Root $Root -ProjectId $ProjectId)
  Ensure-Directory -Path $readPointersPath

  $payload = @{
    entry_id = $entryId
    project_id = $ProjectId
    task_id = $TaskId
    session_id = $SessionId
    agent_id = $AgentId
    timestamp = $timestamp
    type = $typeCode
    type_name = $typeName
    content = $Content
    related_files = $normalizedFiles
    status = $Status
  }
  if ($null -ne $Metadata -and $Metadata.Count -gt 0) {
    $payload["metadata"] = $Metadata
  }
  $payload["triad_file"] = $triadFile
  $payload["checksum"] = New-Sha256Checksum -Text ($payload | ConvertTo-Json -Depth 12 -Compress)

  Append-JsonLine -Path $triadFile -Object $payload
  Append-JsonLine -Path $projectStreamFile -Object $payload
  Refresh-ProjectStreamSnapshots -Root $Root -ProjectId $ProjectId

  $sessionDeltaPath = [string]$location["sessionDeltaFile"]
  $sessionDelta = if (Test-Path -LiteralPath $sessionDeltaPath) { Read-JsonFile -Path $sessionDeltaPath } else { @{} }
  if (-not $sessionDelta.ContainsKey("paths") -or -not ($sessionDelta["paths"] -is [hashtable])) {
    $sessionDelta["paths"] = @{}
  }
  $sessionDelta["paths"]["thoughtLog"] = Get-ProjectSessionTriadFile -Root $Root -ProjectId $ProjectId -SessionId $SessionId -TriadType "THOUGHT" -State $location["state"] -Partition $location["partition"]
  $sessionDelta["paths"]["execLog"] = Get-ProjectSessionTriadFile -Root $Root -ProjectId $ProjectId -SessionId $SessionId -TriadType "EXEC" -State $location["state"] -Partition $location["partition"]
  $sessionDelta["paths"]["modLog"] = Get-ProjectSessionTriadFile -Root $Root -ProjectId $ProjectId -SessionId $SessionId -TriadType "MOD" -State $location["state"] -Partition $location["partition"]
  $sessionDelta["paths"]["streamEntries"] = $projectStreamFile
  $sessionDelta["paths"]["streamNewestEntry"] = $newestEntryFile
  $sessionDelta["paths"]["streamTail"] = $tailFile
  $sessionDelta["paths"]["readPointerDir"] = $readPointersPath
  $sessionDelta["paths"]["liveStateBoard"] = $liveStateBoardFile
  $sessionDelta["paths"]["digestLatestSummary"] = $digestLatestFile
  $sessionDelta["paths"]["stateEventLog"] = (Get-ProjectStateEventLogFile -Root $Root -ProjectId $ProjectId)
  $sessionDelta["paths"]["transactionsRoot"] = (Get-ProjectTransactionsPath -Root $Root -ProjectId $ProjectId)
  $sessionDelta["paths"]["conflictMarkdown"] = $conflictMarkdownFile

  if (-not $sessionDelta.ContainsKey("triadCounters") -or -not ($sessionDelta["triadCounters"] -is [hashtable])) {
    $sessionDelta["triadCounters"] = @{
      thought = 0
      exec = 0
      mod = 0
    }
  }
  $counterKey = switch ($typeCode) {
    "T" { "thought" }
    "E" { "exec" }
    "M" { "mod" }
    default { "exec" }
  }
  $sessionDelta["triadCounters"][$counterKey] = [int]$sessionDelta["triadCounters"][$counterKey] + 1
  $sessionDelta["updatedAt"] = $timestamp
  $sessionDelta["lastTriad"] = @{
    entryId = $entryId
    timestamp = $timestamp
    type = $typeCode
    typeName = $typeName
    status = $Status
    contentPreview = (Get-TextPreview -Text $Content -MaxLength 160)
  }
  $sessionDelta["streamState"] = @{
    lastProducedEntryId = $entryId
    lastProducedAt = $timestamp
    globalStreamFile = $projectStreamFile
    newestEntryFile = $newestEntryFile
    tailFile = $tailFile
    tailWindow = Get-StreamTailSize
    lossyBoundaryPolicy = "stop_and_require_explicit_acceptance"
  }
  Write-JsonFile -Path $sessionDeltaPath -Object $sessionDelta

  $l1EntryFile = Get-ProjectL1EntryFile -Root $Root -ProjectId $ProjectId -SessionId $SessionId
  if (Test-Path -LiteralPath $l1EntryFile) {
    $l1Entry = Read-JsonFile -Path $l1EntryFile
    $l1Entry["updatedAt"] = $timestamp
    $l1Entry["lastTriadEntryId"] = $entryId
    $l1Entry["lastTriadType"] = $typeCode
    $l1Entry["lastTriadStatus"] = $Status
    $l1Entry["lastTriadSummary"] = Get-TextPreview -Text $Content -MaxLength 160
    Write-JsonFile -Path $l1EntryFile -Object $l1Entry
  }

  $registry = Read-ProjectRegistry -Root $Root -ProjectId $ProjectId
  $registry["sessions"] = Upsert-ListEntry -Items $registry["sessions"] -Item @{
    sessionId = $SessionId
    taskId = $TaskId
    owner = $AgentId
    status = if ($sessionDelta.ContainsKey("status")) { [string]$sessionDelta["status"] } else { "active" }
    sessionDeltaFile = $sessionDeltaPath
    latestStreamEntryId = $entryId
    updatedAt = $timestamp
  } -Key "sessionId"
  Write-ProjectRegistry -Root $Root -ProjectId $ProjectId -Registry $registry

  Update-ProjectSessionHeads -Root $Root -ProjectId $ProjectId
  Update-LiveStateBoard -Root $Root -ProjectId $ProjectId

  return @{
    record = $payload
    triadFile = $triadFile
    newestEntryFile = $newestEntryFile
    tailFile = $tailFile
    liveStateBoardFile = $liveStateBoardFile
    sessionDeltaFile = $sessionDeltaPath
  }
}

function Normalize-ProjectRegistry {
  param(
    $Registry,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [hashtable]$Project
  )

  if (-not ($Registry -is [hashtable])) {
    $Registry = @{}
  }

  $Registry["version"] = 2
  $Registry["projectId"] = $ProjectId

  if ($null -ne $Project) {
    foreach ($mapping in @(
      @{ key = "status"; source = "status" },
      @{ key = "repoPath"; source = "repoPath" },
      @{ key = "coordinationPath"; source = "coordinationPath" },
      @{ key = "worktreeRoot"; source = "worktreeRoot" },
      @{ key = "baselineVersion"; source = "baselineVersion" }
    )) {
      if (
        (-not $Registry.ContainsKey($mapping["key"]) -or [string]::IsNullOrWhiteSpace([string]$Registry[$mapping["key"]])) -and
        $Project.ContainsKey($mapping["source"]) -and
        (-not [string]::IsNullOrWhiteSpace([string]$Project[$mapping["source"]]))
      ) {
        $Registry[$mapping["key"]] = [string]$Project[$mapping["source"]]
      }
    }
  }

  if (-not $Registry.ContainsKey("updatedAt") -or [string]::IsNullOrWhiteSpace([string]$Registry["updatedAt"])) {
    $Registry["updatedAt"] = Get-NowIso
  }
  if (-not $Registry.ContainsKey("status") -or [string]::IsNullOrWhiteSpace([string]$Registry["status"])) {
    $Registry["status"] = "active"
  }
  if (-not $Registry.ContainsKey("tasks")) {
    $Registry["tasks"] = @()
  }
  if (-not $Registry.ContainsKey("locks")) {
    $Registry["locks"] = @()
  }
  if (-not $Registry.ContainsKey("sessions")) {
    $Registry["sessions"] = @()
  }
  if (-not $Registry.ContainsKey("tiers") -or -not ($Registry["tiers"] -is [hashtable])) {
    $Registry["tiers"] = @{}
  }

  if ($Registry["tiers"].ContainsKey("l3Archive") -and -not $Registry["tiers"].ContainsKey("l4Archive")) {
    $Registry["tiers"]["l4Archive"] = $Registry["tiers"]["l3Archive"]
  }

  $Registry["tasks"] = ConvertTo-ObjectArray -Value $Registry["tasks"]
  $Registry["locks"] = ConvertTo-ObjectArray -Value $Registry["locks"]
  $Registry["sessions"] = ConvertTo-ObjectArray -Value $Registry["sessions"]

  foreach ($tierKey in @("l1Active", "l2Recent", "l3Digest", "l4Archive")) {
    if (-not $Registry["tiers"].ContainsKey($tierKey)) {
      $Registry["tiers"][$tierKey] = @()
    }
    $Registry["tiers"][$tierKey] = ConvertTo-ObjectArray -Value $Registry["tiers"][$tierKey]
  }

  return $Registry
}

function Write-ProjectRegistry {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId,
    [Parameter(Mandatory = $true)]
    [hashtable]$Registry
  )

  $project = $null
  $projectFile = Get-ProjectFile -Root $Root -ProjectId $ProjectId
  if (Test-Path -LiteralPath $projectFile) {
    $project = Read-JsonFile -Path $projectFile
  }
  $Registry = Normalize-ProjectRegistry -Registry $Registry -ProjectId $ProjectId -Project $project
  $Registry["updatedAt"] = Get-NowIso
  $path = Get-ProjectRegistryFile -Root $Root -ProjectId $ProjectId
  Write-JsonFile -Path $path -Object $Registry
}

function Get-ProjectSessionPromptFiles {
  param(
    [Parameter(Mandatory = $true)]
    [hashtable]$Registry,
    [int]$MaxCount = 5
  )

  $items = @()
  foreach ($session in (ConvertTo-ObjectArray -Value $Registry["sessions"])) {
    if (-not ($session -is [hashtable])) {
      continue
    }
    if (-not $session.ContainsKey("promptFile") -or [string]::IsNullOrWhiteSpace([string]$session["promptFile"])) {
      continue
    }
    $items += ,$session
  }

  $items = $items | Sort-Object {
    if ($_.ContainsKey("updatedAt") -and -not [string]::IsNullOrWhiteSpace([string]($_["updatedAt"]))) {
      [DateTimeOffset]::Parse([string]($_["updatedAt"]))
    }
    else {
      [DateTimeOffset]::MinValue
    }
  } -Descending

  return ,@($items | Select-Object -First $MaxCount)
}

function Update-ProjectAiStartFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  $projectFile = Get-ProjectFile -Root $Root -ProjectId $ProjectId
  if (-not (Test-Path -LiteralPath $projectFile)) {
    return
  }

  $project = Read-JsonFile -Path $projectFile
  $registry = Read-ProjectRegistry -Root $Root -ProjectId $ProjectId
  $activeIndexFile = Get-ProjectActiveSessionsIndexFile -Root $Root -ProjectId $ProjectId
  $activeIndex = if (Test-Path -LiteralPath $activeIndexFile) { Read-JsonFile -Path $activeIndexFile } else { @{ sessions = @() } }
  $activeSessions = if ($activeIndex.ContainsKey("sessions")) { ConvertTo-ObjectArray -Value $activeIndex["sessions"] } else { @() }
  $activeTasks = @((ConvertTo-ObjectArray -Value $registry["tasks"]) | Where-Object {
      $_ -is [hashtable] -and $_.ContainsKey("status") -and ([string]($_["status"])).ToLowerInvariant() -eq "active"
    })
  $recentSessions = if ($registry.ContainsKey("tiers") -and $registry["tiers"].ContainsKey("l2Recent")) { ConvertTo-ObjectArray -Value $registry["tiers"]["l2Recent"] } else { @() }
  $digestEntries = if ($registry.ContainsKey("tiers") -and $registry["tiers"].ContainsKey("l3Digest")) { ConvertTo-ObjectArray -Value $registry["tiers"]["l3Digest"] } else { @() }
  $archivedSessions = if ($registry.ContainsKey("tiers") -and $registry["tiers"].ContainsKey("l4Archive")) { ConvertTo-ObjectArray -Value $registry["tiers"]["l4Archive"] } else { @() }
  $promptSessions = Get-ProjectSessionPromptFiles -Registry $registry -MaxCount 5
  $boardFile = Get-ProjectBoardFile -Root $Root -ProjectId $ProjectId
  $handoffFile = Get-ProjectHandoffFile -Root $Root -ProjectId $ProjectId
  $decisionsFile = Get-ProjectDecisionsFile -Root $Root -ProjectId $ProjectId
  $readmeIndexFile = Get-ProjectReadmeIndexFile -Root $Root -ProjectId $ProjectId
  $liveStateBoardFile = Get-ProjectLiveStateBoardFile -Root $Root -ProjectId $ProjectId
  $sessionHeadsFile = Get-ProjectSessionHeadsFile -Root $Root -ProjectId $ProjectId
  $digestLatestFile = Get-ProjectDigestLatestSummaryFile -Root $Root -ProjectId $ProjectId
  $streamNewestEntryFile = Get-ProjectStreamNewestEntryFile -Root $Root -ProjectId $ProjectId

  $sessionLines = @()
  foreach ($session in ($activeSessions | Sort-Object {
        if ($_.ContainsKey("updatedAt") -and -not [string]::IsNullOrWhiteSpace([string]($_["updatedAt"]))) {
          [DateTimeOffset]::Parse([string]($_["updatedAt"]))
        }
        else {
          [DateTimeOffset]::MinValue
        }
      } -Descending)) {
    $sessionId = if ($session.ContainsKey("sessionId")) { [string]$session["sessionId"] } else { "unknown-session" }
    $taskId = if ($session.ContainsKey("taskId")) { [string]$session["taskId"] } else { "unknown-task" }
    $agentId = if ($session.ContainsKey("agentId")) { [string]$session["agentId"] } elseif ($session.ContainsKey("owner")) { [string]$session["owner"] } else { "unknown-agent" }
    $branch = if ($session.ContainsKey("branch")) { [string]$session["branch"] } else { "unknown-branch" }
    $worktree = if ($session.ContainsKey("worktreePath")) { [string]$session["worktreePath"] } else { "unknown-worktree" }
    $sessionDeltaPath = if ($session.ContainsKey("sessionDeltaFile")) { [string]$session["sessionDeltaFile"] } else { "" }
    $promptPath = ""
    foreach ($promptSession in $promptSessions) {
      if (($promptSession.ContainsKey("sessionId")) -and ([string]$promptSession["sessionId"] -eq $sessionId) -and $promptSession.ContainsKey("promptFile")) {
        $promptPath = [string]$promptSession["promptFile"]
        break
      }
    }

    $line = "- Task `"{0}`" owner=`"{1}`" session=`"{2}`" branch=`"{3}`"" -f $taskId, $agentId, $sessionId, $branch
    if (-not [string]::IsNullOrWhiteSpace($promptPath)) {
      $line += " prompt=`"$promptPath`""
    }
    if (-not [string]::IsNullOrWhiteSpace($sessionDeltaPath)) {
      $line += " delta=`"$sessionDeltaPath`""
    }
    $line += " worktree=`"$worktree`""
    $sessionLines += $line
  }
  if (@($sessionLines).Count -eq 0) {
    $sessionLines = @('- No active sessions. Use `coordination\bin\coord-claim-task.cmd` to start the next task session.')
  }

  $promptLines = @()
  foreach ($promptSession in $promptSessions) {
    if (-not $promptSession.ContainsKey("promptFile")) {
      continue
    }
    $promptLines += '- Session `{0}` -> `{1}`' -f [string]$promptSession["sessionId"], [string]$promptSession["promptFile"]
  }
  if (@($promptLines).Count -eq 0) {
    $promptLines = @("- No generated session prompts are registered yet.")
  }

  $content = @(
    "---"
    "file_type: ai_start"
    "project_id: $ProjectId"
    "write_mode: script_managed"
    "last_updated: $(Get-NowIso)"
    "---"
    "# AI Start"
    ""
    "## Project"
    ""
    ('- Project id: `{0}`' -f $ProjectId)
    ('- Project name: `{0}`' -f [string]$project["name"])
    ('- Repository root: `{0}`' -f [string]$project["repoPath"])
    ('- Coordination root: `{0}`' -f [string]$project["coordinationPath"])
    ('- Worktree root: `{0}`' -f [string]$project["worktreeRoot"])
    ('- Default branch: `{0}`' -f [string]$project["defaultBranch"])
    ('- Baseline version: `{0}`' -f [string]$project["baselineVersion"])
    ""
    "## Minimal Read Chain"
    ""
    '1. `context/LIVE_STATE_BOARD.md`'
    '2. `STREAM_LOG/SESSION_HEADS.json`'
    '3. `CHANGELOG_TIER/L3_DIGEST/latest_summary.md`'
    '4. `CHANGELOG_TIER/L1_ACTIVE/ACTIVE_SESSIONS.json`'
    ('5. use `coordination\bin\coord-read-stream.cmd -ProjectId {0} -AgentId <agent-id> -OtherAgentsOnly -IncludeSessionHeads` for unread live deltas' -f $ProjectId)
    '6. treat `STREAM_LOG/NEWEST_ENTRY.json` and `STREAM_LOG/LATEST_TAIL.json` as summary projections only'
    '7. if assigned, read your generated `SESSIONS/active/<session-id>/AI_PROMPT.md`; otherwise read your task file, lock file, and `SESSIONS/active/<session-id>/SESSION_DELTA.json`'
    '8. `README_INDEX.md` only if you need deeper topology, boundary, or recent-history detail'
    '9. only then the business files inside the repository worktree'
    ""
    "## Expand Only If Needed"
    ""
    '- Read `ARCHITECTURE_TREE.json` and `ARCHITECTURE_INDEX.md` when system layout, dependencies, or ownership boundaries matter.'
    '- Read `CHANGELOG_TIER/L2_RECENT/RECENT_INDEX.json` only if the task overlaps recent finished work.'
    '- Read `CHANGELOG_TIER/L0_BASE/BASELINE_STATE.json` only if baseline version changes or this project is new to you.'
    '- Read `context/BOARD.md`, `context/HANDOFF.md`, and `context/DECISIONS.md` when queue state, baton changes, or durable design decisions matter.'
    ""
    "## Current State"
    ""
    "- Active sessions: $(@($activeSessions).Count)"
    "- Active tasks: $(@($activeTasks).Count)"
    "- Recent sessions: $(@($recentSessions).Count)"
    "- Digest nodes: $(@($digestEntries).Count)"
    "- Archived sessions: $(@($archivedSessions).Count)"
    ""
    "## Active Sessions"
    ""
  ) + $sessionLines + @(
    ""
    "## Shared Context Files"
    ""
    ('- Read gate: `{0}`' -f $readmeIndexFile)
    ('- Live state board: `{0}`' -f $liveStateBoardFile)
    ('- Session heads: `{0}`' -f $sessionHeadsFile)
    ('- Latest digest: `{0}`' -f $digestLatestFile)
    ('- Stream source: `{0}`' -f (Get-ProjectStreamEntriesFile -Root $Root -ProjectId $ProjectId))
    ('- Newest stream summary: `{0}`' -f $streamNewestEntryFile)
    ('- Tail summary window: `{0}`' -f (Get-ProjectStreamTailFile -Root $Root -ProjectId $ProjectId))
    ('- State event log: `{0}`' -f (Get-ProjectStateEventLogFile -Root $Root -ProjectId $ProjectId))
    ('- Transactions root: `{0}`' -f (Get-ProjectTransactionsPath -Root $Root -ProjectId $ProjectId))
    ('- Board: `{0}`' -f $boardFile)
    ('- Handoff: `{0}`' -f $handoffFile)
    ('- Decisions: `{0}`' -f $decisionsFile)
    ""
    "## Session Prompt Files"
    ""
  ) + $promptLines + @(
    ""
    "## Logging Commands"
    ""
    ('- THOUGHT: `coordination\bin\coord-log-thought.cmd -ProjectId {0} -SessionId <session-id> -AgentId <agent-id> -Content "<intent or decision>"`' -f $ProjectId)
    ('- EXEC: `coordination\bin\coord-log-operation.cmd -ProjectId {0} -SessionId <session-id> -AgentId <agent-id> -ActionType <type> -TargetPath <path> -DeltaSummary "<why>"`' -f $ProjectId)
    ('- MOD: `coordination\bin\coord-log-mod.cmd -ProjectId {0} -SessionId <session-id> -AgentId <agent-id> -Content "<what changed>" -RelatedFiles <path>`' -f $ProjectId)
    ('- STREAM: `coordination\bin\coord-read-stream.cmd -ProjectId {0} -AgentId <agent-id> -OtherAgentsOnly -IncludeSessionHeads`' -f $ProjectId)
    ""
    "## Rule"
    ""
    "- Do not inspect business code before the indexed read chain is complete."
    "- Work only in your assigned worktree and branch."
    "- Treat this file as the project-level work packet; outer files should route you here, not duplicate deeper read order."
    "- If you were given a session prompt, read it after steps 1-4 and before business code."
    "- Default collaboration mode is progressive: read the digest head and active stream summaries first, then widen only when the task truly needs it."
  )

  Write-TextFile -Path (Get-ProjectAiStartFile -Root $Root -ProjectId $ProjectId) -Content ([string]::Join([Environment]::NewLine, $content))
}

function Update-CoordinationActiveContext {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  $registry = Read-CoordinationRegistry -Root $Root
  $activeProjects = @((ConvertTo-ObjectArray -Value $registry["projects"]) | Where-Object {
      $_ -is [hashtable] -and
      $_.ContainsKey("id") -and
      $_.ContainsKey("coordinationPath") -and
      (Test-Path -LiteralPath ([string]($_["coordinationPath"])))
    })

  $activeProjects = @($activeProjects | Sort-Object {
    if ($_.ContainsKey("updatedAt") -and -not [string]::IsNullOrWhiteSpace([string]($_["updatedAt"]))) {
      [DateTimeOffset]::Parse([string]($_["updatedAt"]))
    }
    else {
      [DateTimeOffset]::MinValue
    }
  } -Descending)

  $preferredProject = $null
  if (@($activeProjects).Count -gt 0) {
    $preferredProject = $activeProjects[0]
  }

  $projectRecords = @()
  foreach ($projectEntry in $activeProjects) {
    $projectId = [string]$projectEntry["id"]
    $projectRegistry = Read-ProjectRegistry -Root $Root -ProjectId $projectId
    $projectConfig = Read-ProjectConfig -Root $Root -ProjectId $projectId
    $activeIndexFile = Get-ProjectActiveSessionsIndexFile -Root $Root -ProjectId $projectId
    $activeIndex = if (Test-Path -LiteralPath $activeIndexFile) { Read-JsonFile -Path $activeIndexFile } else { @{ sessions = @() } }
    $activeSessions = if ($activeIndex.ContainsKey("sessions")) { ConvertTo-ObjectArray -Value $activeIndex["sessions"] } else { @() }
    $promptFiles = Get-ProjectSessionPromptFiles -Registry $projectRegistry -MaxCount 3
    $digestCount = if ($projectRegistry.ContainsKey("tiers") -and $projectRegistry["tiers"].ContainsKey("l3Digest")) { @((ConvertTo-ObjectArray -Value $projectRegistry["tiers"]["l3Digest"])).Count } else { 0 }
    $archiveCount = if ($projectRegistry.ContainsKey("tiers") -and $projectRegistry["tiers"].ContainsKey("l4Archive")) { @((ConvertTo-ObjectArray -Value $projectRegistry["tiers"]["l4Archive"])).Count } else { 0 }

    $projectRecords += ,@{
      projectId = $projectId
      projectName = [string]$projectConfig["name"]
      repoPath = [string]$projectConfig["repoPath"]
      defaultBranch = [string]$projectConfig["defaultBranch"]
      baselineVersion = [string]$projectConfig["baselineVersion"]
      coordinationPath = [string]$projectConfig["coordinationPath"]
      aiStartFile = Get-ProjectAiStartFile -Root $Root -ProjectId $projectId
      liveStateBoardFile = Get-ProjectLiveStateBoardFile -Root $Root -ProjectId $projectId
      sessionHeadsFile = Get-ProjectSessionHeadsFile -Root $Root -ProjectId $projectId
      digestLatestFile = Get-ProjectDigestLatestSummaryFile -Root $Root -ProjectId $projectId
      streamNewestEntryFile = Get-ProjectStreamNewestEntryFile -Root $Root -ProjectId $projectId
      readmeIndexFile = Get-ProjectReadmeIndexFile -Root $Root -ProjectId $projectId
      activeSessionCount = @($activeSessions).Count
      taskCount = @($projectRegistry["tasks"] | ForEach-Object { $_ }).Count
      digestCount = $digestCount
      archiveCount = $archiveCount
      updatedAt = if ($projectEntry.ContainsKey("updatedAt")) { ConvertTo-IsoText -Value $projectEntry["updatedAt"] } else { Get-NowIso }
      promptFiles = @($promptFiles | ForEach-Object { if ($_.ContainsKey("promptFile")) { [string]($_["promptFile"]) } })
    }
  }

  $preferredProjectId = if ($null -ne $preferredProject) { [string]$preferredProject["id"] } else { $null }
  $readChain = @(
    (Get-CoordinationAiEntryFile -Root $Root),
    (Get-CoordinationActiveContextMarkdownFile -Root $Root)
  )
  if (-not [string]::IsNullOrWhiteSpace($preferredProjectId)) {
    $readChain += @(
      (Get-ProjectAiStartFile -Root $Root -ProjectId $preferredProjectId)
    )
  }

  Write-JsonFile -Path (Get-CoordinationActiveContextJsonFile -Root $Root) -Object @{
    version = 1
    generatedAt = Get-NowIso
    activeProjectCount = @($projectRecords).Count
    preferredProjectId = $preferredProjectId
    aiReadChain = $readChain
    projectAiStartIsAuthoritative = $true
    projects = $projectRecords
  }

  $projectLines = @()
  foreach ($record in $projectRecords) {
    $line = '- Project `{0}` repo=`{1}` active_sessions={2} ai_start=`{3}` live_board=`{4}` session_heads=`{5}` digest=`{6}`' -f $record["projectId"], $record["repoPath"], $record["activeSessionCount"], $record["aiStartFile"], $record["liveStateBoardFile"], $record["sessionHeadsFile"], $record["digestLatestFile"]
    if (@($record["promptFiles"]).Count -gt 0) {
      $line += (' prompts=`{0}`' -f ([string]::Join("; ", @($record["promptFiles"]))))
    }
    $projectLines += $line
  }
  if (@($projectLines).Count -eq 0) {
    $projectLines = @('- No active coordination project runtime exists yet. Run `coordination\bin\coord-init-project.cmd -ProjectName <name> -RepoPath .`.')
  }

  $preferredProjectLines = if ($null -eq $preferredProject) {
    @("- Preferred project: none") + @("- Next action: initialize a project runtime first")
  }
  else {
    @(
      ('- Preferred project: `{0}`' -f $preferredProjectId)
      ('- Next read: `{0}`' -f (Get-ProjectAiStartFile -Root $Root -ProjectId $preferredProjectId))
      '- Then follow that file exactly; it is the authoritative project work packet.'
    )
  }

  $content = @(
    "---"
    "file_type: active_context"
    "write_mode: script_managed"
    "last_updated: $(Get-NowIso)"
    "---"
    "# Active Context"
    ""
    "## AI Read Chain"
    ""
    ('1. `{0}`' -f (Get-CoordinationAiEntryFile -Root $Root))
    ('2. `{0}`' -f (Get-CoordinationActiveContextMarkdownFile -Root $Root))
  )

  if ($null -ne $preferredProject) {
    $content += @(
      ('3. `{0}`' -f (Get-ProjectAiStartFile -Root $Root -ProjectId $preferredProjectId))
      '4. follow that project''s `AI_START.md` exactly'
      '5. if assigned, read your session prompt when `AI_START.md` tells you to'
    )
  }

  $content += @(
    ""
    "## Preferred Project"
    ""
  ) + $preferredProjectLines + @(
    ""
    "## Active Projects"
    ""
  ) + $projectLines + @(
    ""
    "## Rule"
    ""
    "- Read this file before you choose a project runtime."
    '- If the operator already gave you a session prompt, still read the preferred project''s `AI_START.md` first.'
    '- Do not duplicate project-level read order outside `AI_START.md`; it is the authoritative work packet for that project.'
    '- Do not scan repository business directories until the chosen project''s `AI_START.md` allows it.'
  )

  Write-TextFile -Path (Get-CoordinationActiveContextMarkdownFile -Root $Root) -Content ([string]::Join([Environment]::NewLine, $content))
}

function Update-CoordinationRuntimeIndexes {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root
  )

  $projectsRoot = Get-CoordinationProjectsRoot -Root $Root
  if (Test-Path -LiteralPath $projectsRoot) {
    foreach ($dir in (Get-ChildItem -LiteralPath $projectsRoot -Directory -ErrorAction SilentlyContinue)) {
      $projectFile = Join-Path $dir.FullName "project.json"
      if (Test-Path -LiteralPath $projectFile) {
        Update-ProjectSessionHeads -Root $Root -ProjectId $dir.Name
        Update-LiveStateBoard -Root $Root -ProjectId $dir.Name
        Update-ProjectAiStartFile -Root $Root -ProjectId $dir.Name
      }
    }
  }

  Update-CoordinationActiveContext -Root $Root
}

function Upsert-CoordinationProject {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [hashtable]$Project
  )

  $registry = Read-CoordinationRegistry -Root $Root
  $registry["projects"] = Upsert-ListEntry -Items $registry["projects"] -Item $Project -Key "id"
  $registry["archivedProjects"] = Remove-ListEntry -Items $registry["archivedProjects"] -Key "id" -Value ([string]$Project["id"])
  Write-CoordinationRegistry -Root $Root -Registry $registry
}

function Remove-CoordinationProject {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [string]$ProjectId
  )

  $registry = Read-CoordinationRegistry -Root $Root
  $registry["projects"] = Remove-ListEntry -Items $registry["projects"] -Key "id" -Value $ProjectId
  Write-CoordinationRegistry -Root $Root -Registry $registry
}

function Upsert-ArchivedCoordinationProject {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [hashtable]$Project
  )

  $registry = Read-CoordinationRegistry -Root $Root
  $registry["projects"] = Remove-ListEntry -Items $registry["projects"] -Key "id" -Value ([string]$Project["id"])
  $registry["archivedProjects"] = Upsert-ListEntry -Items $registry["archivedProjects"] -Item $Project -Key "id"
  Write-CoordinationRegistry -Root $Root -Registry $registry
}

function Get-NowIso {
  return [DateTimeOffset]::Now.ToString("o")
}

function ConvertTo-IsoText {
  param(
    $Value
  )

  if ($null -eq $Value) {
    return ""
  }

  if ($Value -is [DateTimeOffset]) {
    return $Value.ToString("o")
  }

  if ($Value -is [DateTime]) {
    return ([DateTimeOffset]$Value).ToString("o")
  }

  $text = [string]$Value
  if ([string]::IsNullOrWhiteSpace($text)) {
    return ""
  }

  $parsed = [DateTimeOffset]::MinValue
  if ([DateTimeOffset]::TryParse($text, [ref]$parsed)) {
    return $parsed.ToString("o")
  }

  return $text
}

function ConvertFrom-JsonSafe {
  param(
    [string]$Text
  )

  if ([string]::IsNullOrWhiteSpace($Text)) {
    return $null
  }

  try {
    return ConvertTo-PlainObject (ConvertFrom-Json $Text)
  }
  catch {
    return $null
  }
}

function Get-CodeCommand {
  param(
    [Parameter(Mandatory = $true)]
    [hashtable]$Config,
    [string]$Override
  )

  if (-not [string]::IsNullOrWhiteSpace($Override)) {
    return $Override
  }

  $configured = [string](Get-ConfigValue -Config $Config -Path "codex.codeCommand" -Default "")
  if (-not [string]::IsNullOrWhiteSpace($configured)) {
    return $configured
  }

  $command = Get-Command code -ErrorAction SilentlyContinue
  if ($command) {
    return $command.Source
  }

  return $null
}

function New-CodexSubmitUri {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Text,
    [Parameter(Mandatory = $true)]
    [hashtable]$Config,
    [string]$ConversationId
  )

  $base = [string](Get-ConfigValue -Config $Config -Path "codex.uriBase" -Default "vscode://openai.chatgpt/submit_prompt")
  $pairs = @("text=$([System.Uri]::EscapeDataString($Text))")

  if (-not [string]::IsNullOrWhiteSpace($ConversationId)) {
    $pairs += "conversationId=$([System.Uri]::EscapeDataString($ConversationId))"
  }

  return ("{0}?{1}" -f $base, [string]::Join("&", $pairs))
}

function Get-PromptOutputDirectory {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [Parameter(Mandatory = $true)]
    [hashtable]$Config
  )

  $configured = [string](Get-ConfigValue -Config $Config -Path "paths.promptOutDir" -Default "")
  if (-not [string]::IsNullOrWhiteSpace($configured)) {
    return $configured
  }

  return (Join-Path $Root "tmp\ops\codex-prompts")
}

function Get-TextPreview {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Text,
    [int]$MaxLength = 120
  )

  if ($Text.Length -le $MaxLength) {
    return $Text
  }

  return $Text.Substring(0, $MaxLength) + "..."
}

function Write-JsonResult {
  param(
    [Parameter(Mandatory = $true)]
    $Object
  )

  $Object | ConvertTo-Json -Depth 8
}
