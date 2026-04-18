[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectId,
  [string]$ArchiveReason = "manual_archive",
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $project = Read-ProjectConfig -Root $root -ProjectId $ProjectId
  $registry = Read-ProjectRegistry -Root $root -ProjectId $ProjectId
  $activeSessionsIndex = Read-JsonFile -Path (Get-ProjectActiveSessionsIndexFile -Root $root -ProjectId $ProjectId)
  $activeSessionIds = @()

  if ($activeSessionsIndex.ContainsKey("sessions") -and $null -ne $activeSessionsIndex["sessions"]) {
    foreach ($entry in ($activeSessionsIndex["sessions"] | ForEach-Object { $_ })) {
      if ($entry.ContainsKey("sessionId")) {
        $activeSessionIds += [string]$entry["sessionId"]
      }
    }
  }

  if ($registry.ContainsKey("tiers") -and ($registry["tiers"] -is [hashtable]) -and $registry["tiers"].ContainsKey("l1Active")) {
    foreach ($entry in ($registry["tiers"]["l1Active"] | ForEach-Object { $_ })) {
      if ($entry.ContainsKey("sessionId")) {
        $activeSessionIds += [string]$entry["sessionId"]
      }
    }
  }

  $activeSessionIds = @($activeSessionIds | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
  if ($activeSessionIds.Count -gt 0) {
    throw ("Project {0} still has active sessions: {1}. Close them before archive." -f $ProjectId, ([string]::Join(", ", $activeSessionIds)))
  }

  $projectPath = Get-ProjectCoordinationPath -Root $root -ProjectId $ProjectId
  $worktreeRoot = Get-ProjectWorktreeRoot -Root $root -ProjectId $ProjectId
  $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
  $now = Get-NowIso
  $archiveProjectRoot = Join-Path (Get-CoordinationArchivedProjectsRoot -Root $root) $ProjectId
  $archiveWorktreeRoot = Join-Path (Get-CoordinationArchivedWorktreesRoot -Root $root) $ProjectId
  $archivedProjectPath = Join-Path $archiveProjectRoot $timestamp
  $archivedWorktreePath = Join-Path $archiveWorktreeRoot $timestamp
  $archiveManifestPath = Join-Path $archivedProjectPath "ARCHIVE_MANIFEST.json"

  if ($DryRun) {
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      projectId = $ProjectId
      archivedProjectPath = $archivedProjectPath
      archivedWorktreePath = if (Test-Path -LiteralPath $worktreeRoot) { $archivedWorktreePath } else { $null }
      archiveReason = $ArchiveReason
      taskCount = @($registry["tasks"] | ForEach-Object { $_ }).Count
      sessionCount = @($registry["sessions"] | ForEach-Object { $_ }).Count
      recentCount = if ($registry.ContainsKey("tiers") -and $registry["tiers"].ContainsKey("l2Recent")) { @($registry["tiers"]["l2Recent"] | ForEach-Object { $_ }).Count } else { 0 }
      digestCount = if ($registry.ContainsKey("tiers") -and $registry["tiers"].ContainsKey("l3Digest")) { @($registry["tiers"]["l3Digest"] | ForEach-Object { $_ }).Count } else { 0 }
      archivedCount = if ($registry.ContainsKey("tiers") -and $registry["tiers"].ContainsKey("l4Archive")) { @($registry["tiers"]["l4Archive"] | ForEach-Object { $_ }).Count } else { 0 }
    }
    exit 0
  }

  Ensure-Directory -Path $archiveProjectRoot
  Ensure-Directory -Path $archiveWorktreeRoot

  if (-not (Test-Path -LiteralPath $projectPath)) {
    throw "Project coordination path does not exist: $projectPath"
  }

  Move-Item -LiteralPath $projectPath -Destination $archivedProjectPath

  $movedWorktrees = $false
  if (Test-Path -LiteralPath $worktreeRoot) {
    Move-Item -LiteralPath $worktreeRoot -Destination $archivedWorktreePath
    $movedWorktrees = $true
  }

  $archivedProjectFile = Join-Path $archivedProjectPath "project.json"
  $archivedRegistryFile = Join-Path $archivedProjectPath "state\registry.json"

  $archivedProject = Read-JsonFile -Path $archivedProjectFile
  $archivedProject["status"] = "archived"
  $archivedProject["archiveReason"] = $ArchiveReason
  $archivedProject["archivedAt"] = $now
  $archivedProject["originalCoordinationPath"] = $projectPath
  $archivedProject["coordinationPath"] = $archivedProjectPath
  $archivedProject["originalWorktreeRoot"] = $worktreeRoot
  $archivedProject["worktreeRoot"] = if ($movedWorktrees) { $archivedWorktreePath } else { $worktreeRoot }
  $archivedProject["updatedAt"] = $now
  Write-JsonFile -Path $archivedProjectFile -Object $archivedProject

  $archivedRegistry = Read-JsonFile -Path $archivedRegistryFile
  $archivedRegistry["status"] = "archived"
  $archivedRegistry["archiveReason"] = $ArchiveReason
  $archivedRegistry["archivedAt"] = $now
  $archivedRegistry["originalCoordinationPath"] = $projectPath
  $archivedRegistry["coordinationPath"] = $archivedProjectPath
  $archivedRegistry["worktreeRoot"] = if ($movedWorktrees) { $archivedWorktreePath } else { $worktreeRoot }
  $archivedRegistry["updatedAt"] = $now
  Write-JsonFile -Path $archivedRegistryFile -Object $archivedRegistry

  Write-JsonFile -Path $archiveManifestPath -Object @{
    version = 1
    projectId = $ProjectId
    archivedAt = $now
    archiveReason = $ArchiveReason
    repoPath = [string]$project["repoPath"]
    originalCoordinationPath = $projectPath
    archivedCoordinationPath = $archivedProjectPath
    originalWorktreeRoot = $worktreeRoot
    archivedWorktreeRoot = if ($movedWorktrees) { $archivedWorktreePath } else { $null }
    summary = @{
      taskCount = @($registry["tasks"] | ForEach-Object { $_ }).Count
      sessionCount = @($registry["sessions"] | ForEach-Object { $_ }).Count
      l2RecentCount = if ($registry.ContainsKey("tiers") -and $registry["tiers"].ContainsKey("l2Recent")) { @($registry["tiers"]["l2Recent"] | ForEach-Object { $_ }).Count } else { 0 }
      l3DigestCount = if ($registry.ContainsKey("tiers") -and $registry["tiers"].ContainsKey("l3Digest")) { @($registry["tiers"]["l3Digest"] | ForEach-Object { $_ }).Count } else { 0 }
      l4ArchiveCount = if ($registry.ContainsKey("tiers") -and $registry["tiers"].ContainsKey("l4Archive")) { @($registry["tiers"]["l4Archive"] | ForEach-Object { $_ }).Count } else { 0 }
    }
  }

  Upsert-ArchivedCoordinationProject -Root $root -Project @{
    id = $ProjectId
    name = [string]$project["name"]
    version = if ($project.ContainsKey("version")) { $project["version"] } else { 2 }
    status = "archived"
    repoPath = [string]$project["repoPath"]
    defaultBranch = [string]$project["defaultBranch"]
    baselineVersion = [string]$project["baselineVersion"]
    coordinationPath = $archivedProjectPath
    worktreeRoot = if ($movedWorktrees) { $archivedWorktreePath } else { $worktreeRoot }
    originalCoordinationPath = $projectPath
    originalWorktreeRoot = $worktreeRoot
    archiveReason = $ArchiveReason
    archivedAt = $now
    createdAt = if ($project.ContainsKey("createdAt")) { [string]$project["createdAt"] } else { $now }
    updatedAt = $now
  }
  Update-CoordinationRuntimeIndexes -Root $root

  Write-JsonResult @{
    ok = $true
    dryRun = $false
    projectId = $ProjectId
    archivedProjectPath = $archivedProjectPath
    archivedWorktreePath = if ($movedWorktrees) { $archivedWorktreePath } else { $null }
    archiveManifest = $archiveManifestPath
    archiveReason = $ArchiveReason
    activeContextFile = (Get-CoordinationActiveContextMarkdownFile -Root $root)
    nextStep = "The project runtime is archived. Re-initialize with coord-init-project only if active multi-AI work resumes."
  }
}
catch {
  Write-JsonResult @{
    ok = $false
    dryRun = [bool]$DryRun
    error = $_.Exception.Message
  }
  exit 1
}
