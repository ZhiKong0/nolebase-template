[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectId,
  [Parameter(Mandatory = $true)]
  [string]$TaskId,
  [string]$AgentName,
  [string]$SessionId,
  [string]$WorktreePath,
  [string]$BranchName,
  [switch]$DeleteBranchIfMerged,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

function New-SessionIndex {
  param([string]$Type)

  switch ($Type) {
    "recent" {
      return @{
        version = 1
        updatedAt = Get-NowIso
        retentionHours = Get-RecentSessionRetentionHours
        retentionCount = Get-RecentSessionRetentionCount
        sessions = @()
      }
    }
    default {
      return @{
        version = 1
        updatedAt = Get-NowIso
        sessions = @()
      }
    }
  }
}

try {
  if ($TaskId -notmatch "^[A-Za-z0-9._-]+$") {
    throw "TaskId must match ^[A-Za-z0-9._-]+$."
  }

  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $project = Read-ProjectConfig -Root $root -ProjectId $ProjectId
  $tasksPath = Get-ProjectTasksPath -Root $root -ProjectId $ProjectId
  $locksPath = Get-ProjectLocksPath -Root $root -ProjectId $ProjectId
  $repoRoot = Resolve-GitRepositoryRoot -Path ([string]$project["repoPath"])
  $taskFile = Join-Path $tasksPath ($TaskId + ".md")
  $lockFile = Join-Path $locksPath ($TaskId + ".json")

  $resolvedWorktreePath = $WorktreePath
  $resolvedBranchName = $BranchName
  $resolvedOwner = $AgentName
  $resolvedSessionId = $SessionId
  $operationLogPath = $null

  if (Test-Path -LiteralPath $lockFile) {
    $lock = Read-JsonFile -Path $lockFile
    if ([string]::IsNullOrWhiteSpace($resolvedWorktreePath) -and $lock.ContainsKey("worktreePath")) {
      $resolvedWorktreePath = [string]$lock["worktreePath"]
    }
    if ([string]::IsNullOrWhiteSpace($resolvedBranchName) -and $lock.ContainsKey("branch")) {
      $resolvedBranchName = [string]$lock["branch"]
    }
    if ([string]::IsNullOrWhiteSpace($resolvedOwner) -and $lock.ContainsKey("owner")) {
      $resolvedOwner = [string]$lock["owner"]
    }
    if ([string]::IsNullOrWhiteSpace($resolvedSessionId) -and $lock.ContainsKey("sessionId")) {
      $resolvedSessionId = [string]$lock["sessionId"]
    }
    if ($lock.ContainsKey("operationLogPath")) {
      $operationLogPath = [string]$lock["operationLogPath"]
    }
  }

  if ([string]::IsNullOrWhiteSpace($resolvedBranchName) -and -not [string]::IsNullOrWhiteSpace($resolvedOwner)) {
    $resolvedBranchName = "ai/{0}/{1}" -f (ConvertTo-Slug -Text $resolvedOwner), (ConvertTo-Slug -Text $TaskId)
  }

  if ([string]::IsNullOrWhiteSpace($resolvedWorktreePath)) {
    if ([string]::IsNullOrWhiteSpace($resolvedOwner)) {
      throw "Cannot infer worktree path. Pass -WorktreePath or keep a lock file for the task."
    }
    $resolvedWorktreePath = Join-Path ([string]$project["worktreeRoot"]) ((ConvertTo-Slug -Text $resolvedOwner) + "-" + (ConvertTo-Slug -Text $TaskId))
  }

  $activeSessionsIndexFile = Get-ProjectActiveSessionsIndexFile -Root $root -ProjectId $ProjectId
  $recentIndexFile = Get-ProjectRecentIndexFile -Root $root -ProjectId $ProjectId
  $archiveIndexFile = Get-ProjectArchiveIndexFile -Root $root -ProjectId $ProjectId
  $activeSessionDeltaFile = if (-not [string]::IsNullOrWhiteSpace($resolvedSessionId)) { Get-ProjectSessionDeltaFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "active" } else { $null }
  $activeSessionDir = if (-not [string]::IsNullOrWhiteSpace($resolvedSessionId)) { Get-ProjectSessionDirectory -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "active" } else { $null }
  $l1EntryFile = if (-not [string]::IsNullOrWhiteSpace($resolvedSessionId)) { Get-ProjectL1EntryFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId } else { $null }

  $now = Get-NowIso
  $todayPartition = Get-Date -Format "yyyy-MM-dd"
  $archiveSessionDir = if (-not [string]::IsNullOrWhiteSpace($resolvedSessionId)) { Get-ProjectSessionDirectory -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "archive" -Partition $todayPartition } else { $null }
  $archiveSessionDeltaFile = if (-not [string]::IsNullOrWhiteSpace($resolvedSessionId)) { Get-ProjectSessionDeltaFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "archive" -Partition $todayPartition } else { $null }
  $recentEntryFile = if (-not [string]::IsNullOrWhiteSpace($resolvedSessionId)) { Get-ProjectRecentEntryFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -DatePartition $todayPartition } else { $null }

  $removeCommand = Format-CommandLine -FilePath (Get-GitCommand) -Arguments @("-C", $repoRoot, "worktree", "remove", $resolvedWorktreePath)
  $deleteCommand = if (-not [string]::IsNullOrWhiteSpace($resolvedBranchName)) {
    Format-CommandLine -FilePath (Get-GitCommand) -Arguments @("-C", $repoRoot, "branch", "-d", $resolvedBranchName)
  }
  else {
    $null
  }

  if ($DryRun) {
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      projectId = $ProjectId
      taskId = $TaskId
      sessionId = $resolvedSessionId
      owner = $resolvedOwner
      worktreePath = $resolvedWorktreePath
      branchName = $resolvedBranchName
      removeCommand = $removeCommand
      deleteBranchCommand = if ($DeleteBranchIfMerged) { $deleteCommand } else { $null }
      taskFile = $taskFile
      lockFile = $lockFile
      archiveSessionDeltaFile = $archiveSessionDeltaFile
      recentEntryFile = $recentEntryFile
      dispatchedAt = $now
    }
    exit 0
  }

  $gitWarnings = @()
  if (Test-Path -LiteralPath $resolvedWorktreePath) {
    $removeResult = Invoke-GitCapture -RepoPath $repoRoot -Arguments @("worktree", "remove", $resolvedWorktreePath)
    if (-not $removeResult.ok) {
      throw ("git worktree remove failed: " + $removeResult.stderr)
    }
  }
  else {
    $gitWarnings += "Worktree path did not exist; coordination files were still updated."
  }

  if ($DeleteBranchIfMerged -and -not [string]::IsNullOrWhiteSpace($resolvedBranchName)) {
    $branchDeleteResult = Invoke-GitCapture -RepoPath $repoRoot -Arguments @("branch", "-d", $resolvedBranchName)
    if (-not $branchDeleteResult.ok) {
      $gitWarnings += ("Branch was not deleted: " + $branchDeleteResult.stderr)
    }
  }

  $sessionDelta = $null
  if (-not [string]::IsNullOrWhiteSpace($resolvedSessionId) -and (Test-Path -LiteralPath $activeSessionDeltaFile)) {
    $sessionDelta = Read-JsonFile -Path $activeSessionDeltaFile
    if ($sessionDelta.ContainsKey("paths") -and $sessionDelta["paths"].ContainsKey("operationLog")) {
      $operationLogPath = [string]$sessionDelta["paths"]["operationLog"]
    }
  }

  if (-not [string]::IsNullOrWhiteSpace($operationLogPath)) {
    Append-JsonLine -Path $operationLogPath -Object @{
      timestamp = $now
      operation_id = "OP-{0}-{1}" -f (Get-Date -Format "yyyyMMddHHmmss"), "session-close"
      project_id = $ProjectId
      session_id = $resolvedSessionId
      task_id = $TaskId
      agent_id = $resolvedOwner
      action_type = "session_close"
      target_path = ($resolvedWorktreePath -replace "\\", "/")
      delta_summary = "Released worktree and archived session state."
      status = "completed"
    }
  }

  if (Test-Path -LiteralPath $lockFile) {
    $lock = Read-JsonFile -Path $lockFile
    if (-not $lock.ContainsKey("scope") -or $null -eq $lock["scope"]) {
      $lock["scope"] = @()
    }
    $lock["status"] = "released"
    $lock["updatedAt"] = $now
    $lock["releasedAt"] = $now
    Write-JsonFile -Path $lockFile -Object $lock
  }

  $recentRecord = $null
  if (-not [string]::IsNullOrWhiteSpace($resolvedSessionId)) {
    if ($null -eq $sessionDelta) {
      $sessionDelta = @{
        version = 1
        sessionId = $resolvedSessionId
        projectId = $ProjectId
        taskId = $TaskId
        agentId = $resolvedOwner
        status = "released"
        updatedAt = $now
        branch = $resolvedBranchName
        worktreePath = $resolvedWorktreePath
        delta = @{
          summary = ""
          touchedFiles = @()
          pendingWrites = @()
          conflicts = @()
        }
      }
    }

    $sessionDelta["status"] = "released"
    $sessionDelta["updatedAt"] = $now
    $sessionDelta["releasedAt"] = $now

    if ($sessionDelta.ContainsKey("paths") -and ($sessionDelta["paths"] -is [hashtable])) {
      foreach ($pathKey in @($sessionDelta["paths"].Keys)) {
        $pathValue = $sessionDelta["paths"][$pathKey]
        if (($pathValue -is [string]) -and -not [string]::IsNullOrWhiteSpace([string]$pathValue) -and ($pathValue -like ($activeSessionDir + "*"))) {
          $sessionDelta["paths"][$pathKey] = $pathValue.Replace($activeSessionDir, $archiveSessionDir)
        }
      }
      $sessionDelta["paths"]["archivedSessionDelta"] = $archiveSessionDeltaFile
      $sessionDelta["paths"]["archivedSessionDirectory"] = $archiveSessionDir
    }

    Ensure-Directory -Path (Split-Path -Parent $archiveSessionDir)
    if (Test-Path -LiteralPath $activeSessionDir) {
      if (Test-Path -LiteralPath $archiveSessionDir) {
        Remove-Item -LiteralPath $archiveSessionDir -Recurse -Force
      }
      Move-Item -LiteralPath $activeSessionDir -Destination $archiveSessionDir
    }
    else {
      Ensure-Directory -Path $archiveSessionDir
    }
    Write-JsonFile -Path $archiveSessionDeltaFile -Object $sessionDelta

    if ((Test-Path -LiteralPath $l1EntryFile)) {
      Remove-Item -LiteralPath $l1EntryFile -Force
    }

    $recentRecord = @{
      version = 1
      sessionId = $resolvedSessionId
      taskId = $TaskId
      owner = $resolvedOwner
      branch = $resolvedBranchName
      worktreePath = $resolvedWorktreePath
      releasedAt = $now
      archivedSessionDelta = $archiveSessionDeltaFile
      promptFile = if ($sessionDelta.ContainsKey("paths") -and $sessionDelta["paths"].ContainsKey("promptFile")) { [string]$sessionDelta["paths"]["promptFile"] } else { "" }
      thoughtLogFile = if ($sessionDelta.ContainsKey("paths") -and $sessionDelta["paths"].ContainsKey("thoughtLog")) { [string]$sessionDelta["paths"]["thoughtLog"] } else { "" }
      execLogFile = if ($sessionDelta.ContainsKey("paths") -and $sessionDelta["paths"].ContainsKey("execLog")) { [string]$sessionDelta["paths"]["execLog"] } else { "" }
      modLogFile = if ($sessionDelta.ContainsKey("paths") -and $sessionDelta["paths"].ContainsKey("modLog")) { [string]$sessionDelta["paths"]["modLog"] } else { "" }
      deltaSummary = if ($sessionDelta.ContainsKey("delta") -and $sessionDelta["delta"].ContainsKey("summary")) { [string]$sessionDelta["delta"]["summary"] } else { "" }
      file = $recentEntryFile
    }
    Ensure-Directory -Path (Split-Path -Parent $recentEntryFile)
    Write-JsonFile -Path $recentEntryFile -Object $recentRecord
  }

  $activeIndex = Read-JsonFile -Path $activeSessionsIndexFile
  if (-not $activeIndex.ContainsKey("sessions")) {
    $activeIndex = New-SessionIndex -Type "active"
  }
  if (-not [string]::IsNullOrWhiteSpace($resolvedSessionId)) {
    $activeIndex["sessions"] = Remove-ListEntry -Items $activeIndex["sessions"] -Key "sessionId" -Value $resolvedSessionId
  }
  $activeIndex["updatedAt"] = $now
  Write-JsonFile -Path $activeSessionsIndexFile -Object $activeIndex

  $recentIndex = Read-JsonFile -Path $recentIndexFile
  if (-not $recentIndex.ContainsKey("sessions")) {
    $recentIndex = New-SessionIndex -Type "recent"
  }
  if ($null -ne $recentRecord) {
    $recentIndex["sessions"] = Upsert-ListEntry -Items $recentIndex["sessions"] -Item $recentRecord -Key "sessionId"
  }

  $archiveIndex = Read-JsonFile -Path $archiveIndexFile
  if (-not $archiveIndex.ContainsKey("sessions")) {
    $archiveIndex = New-SessionIndex -Type "archive"
  }

  $recentSessions = @($recentIndex["sessions"] | ForEach-Object { $_ })
  $recentSessions = $recentSessions | Sort-Object {
    if ($_.ContainsKey("releasedAt")) {
      [DateTimeOffset]::Parse([string]$_["releasedAt"])
    }
    else {
      [DateTimeOffset]::MinValue
    }
  } -Descending

  $retentionCutoff = [DateTimeOffset]::Now.AddHours(- (Get-RecentSessionRetentionHours))
  $keepCount = Get-RecentSessionRetentionCount
  $kept = @()
  $toArchive = @()
  $index = 0
  foreach ($entry in $recentSessions) {
    $releasedAt = if ($entry.ContainsKey("releasedAt")) { [DateTimeOffset]::Parse([string]$entry["releasedAt"]) } else { [DateTimeOffset]::MinValue }
    if (($releasedAt -lt $retentionCutoff) -or ($index -ge $keepCount)) {
      $toArchive += ,$entry
    }
    else {
      $kept += ,$entry
    }
    $index += 1
  }

  foreach ($entry in $toArchive) {
    $releasedAt = if ($entry.ContainsKey("releasedAt")) { [DateTimeOffset]::Parse([string]$entry["releasedAt"]) } else { [DateTimeOffset]::Now }
    $archiveMonth = $releasedAt.ToString("yyyy-MM")
    $archiveFile = Get-ProjectArchiveEntryFile -Root $root -ProjectId $ProjectId -SessionId ([string]$entry["sessionId"]) -MonthPartition $archiveMonth
    Ensure-Directory -Path (Split-Path -Parent $archiveFile)
    $archiveEntry = @{}
    foreach ($key in $entry.Keys) {
      $archiveEntry[$key] = $entry[$key]
    }
    $archiveEntry["file"] = $archiveFile
    $archiveEntry["archivedAt"] = $now

    if ($entry.ContainsKey("file") -and (Test-Path -LiteralPath [string]$entry["file"])) {
      Move-Item -LiteralPath ([string]$entry["file"]) -Destination $archiveFile -Force
    }
    else {
      Write-JsonFile -Path $archiveFile -Object $archiveEntry
    }

    $archiveIndex["sessions"] = Upsert-ListEntry -Items $archiveIndex["sessions"] -Item $archiveEntry -Key "sessionId"
  }

  $recentIndex["sessions"] = ,@($kept)
  $recentIndex["updatedAt"] = $now
  $archiveIndex["updatedAt"] = $now
  Write-JsonFile -Path $recentIndexFile -Object $recentIndex
  Write-JsonFile -Path $archiveIndexFile -Object $archiveIndex

  $registry = Read-ProjectRegistry -Root $root -ProjectId $ProjectId
  if (-not $registry.ContainsKey("tiers") -or -not ($registry["tiers"] -is [hashtable])) {
    $registry["tiers"] = @{
      l1Active = @()
      l2Recent = @()
      l3Digest = @()
      l4Archive = @()
    }
  }

  $registry["tasks"] = Upsert-ListEntry -Items $registry["tasks"] -Item @{
    taskId = $TaskId
    sessionId = $resolvedSessionId
    owner = $resolvedOwner
    branch = $resolvedBranchName
    worktreePath = $resolvedWorktreePath
    taskFile = $taskFile
    lockFile = $lockFile
    status = "released"
    updatedAt = $now
  } -Key "taskId"
  $registry["locks"] = Upsert-ListEntry -Items $registry["locks"] -Item @{
    taskId = $TaskId
    sessionId = $resolvedSessionId
    owner = $resolvedOwner
    branch = $resolvedBranchName
    worktreePath = $resolvedWorktreePath
    status = "released"
    updatedAt = $now
  } -Key "taskId"
  if (-not [string]::IsNullOrWhiteSpace($resolvedSessionId)) {
    $registry["sessions"] = Upsert-ListEntry -Items $registry["sessions"] -Item @{
      sessionId = $resolvedSessionId
      taskId = $TaskId
      owner = $resolvedOwner
      branch = $resolvedBranchName
      worktreePath = $resolvedWorktreePath
      taskFile = $taskFile
      lockFile = $lockFile
      sessionDeltaFile = $archiveSessionDeltaFile
      operationLogPath = $operationLogPath
      status = "released"
      releasedAt = $now
      updatedAt = $now
    } -Key "sessionId"
    $registry["tiers"]["l1Active"] = Remove-ListEntry -Items $registry["tiers"]["l1Active"] -Key "sessionId" -Value $resolvedSessionId
    $registry["tiers"]["l2Recent"] = @()
    foreach ($keptEntry in $kept) {
      $registry["tiers"]["l2Recent"] += ,@{
        sessionId = [string]$keptEntry["sessionId"]
        taskId = [string]$keptEntry["taskId"]
        file = [string]$keptEntry["file"]
        updatedAt = $now
      }
    }
    $registry["tiers"]["l4Archive"] = @()
    foreach ($archivedEntry in $archiveIndex["sessions"]) {
      $registry["tiers"]["l4Archive"] += ,@{
        sessionId = [string]$archivedEntry["sessionId"]
        taskId = [string]$archivedEntry["taskId"]
        file = [string]$archivedEntry["file"]
        updatedAt = $now
      }
    }
  }
  Write-ProjectRegistry -Root $root -ProjectId $ProjectId -Registry $registry

  $project["updatedAt"] = $now
  Write-JsonFile -Path (Get-ProjectFile -Root $root -ProjectId $ProjectId) -Object $project
  Upsert-CoordinationProject -Root $root -Project $project
  Update-CoordinationRuntimeIndexes -Root $root

  Write-JsonResult @{
    ok = $true
    dryRun = $false
    projectId = $ProjectId
    taskId = $TaskId
    sessionId = $resolvedSessionId
    owner = $resolvedOwner
    worktreePath = $resolvedWorktreePath
    branchName = $resolvedBranchName
    activeContextFile = (Get-CoordinationActiveContextMarkdownFile -Root $root)
    aiStartFile = (Get-ProjectAiStartFile -Root $root -ProjectId $ProjectId)
    warnings = if ($gitWarnings.Count -gt 0) { $gitWarnings } else { $null }
    nextStep = "Update BOARD.md and HANDOFF.md, then continue future work from L2_RECENT or a new active session."
    dispatchedAt = $now
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
