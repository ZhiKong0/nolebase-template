[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectId,
  [Parameter(Mandatory = $true)]
  [string]$SessionId,
  [Parameter(Mandatory = $true)]
  [string]$AgentId,
  [Parameter(Mandatory = $true)]
  [string]$ActionType,
  [Parameter(Mandatory = $true)]
  [string]$TargetPath,
  [Parameter(Mandatory = $true)]
  [string]$DeltaSummary,
  [string]$Status = "planned",
  [string]$OperationId,
  [string]$TaskId,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $project = Read-ProjectConfig -Root $root -ProjectId $ProjectId
  $repoRoot = Resolve-GitRepositoryRoot -Path ([string]$project["repoPath"])
  $sessionDeltaPath = Resolve-ProjectSessionDeltaPath -Root $root -ProjectId $ProjectId -SessionId $SessionId
  if ([string]::IsNullOrWhiteSpace($sessionDeltaPath)) {
    throw "Session delta was not found for session: $SessionId"
  }

  $sessionDelta = Read-JsonFile -Path $sessionDeltaPath
  if ([string]::IsNullOrWhiteSpace($TaskId) -and $sessionDelta.ContainsKey("taskId")) {
    $TaskId = [string]$sessionDelta["taskId"]
  }

  $targetRelativePath = Normalize-RepoRelativePath -RepoRoot $repoRoot -Path $TargetPath
  $operationIdValue = if (-not [string]::IsNullOrWhiteSpace($OperationId)) {
    $OperationId
  }
  else {
    "OP-{0}-{1}" -f (Get-Date -Format "yyyyMMddHHmmss"), ([guid]::NewGuid().ToString("N").Substring(0, 6))
  }

  $operationLogPath = if ($sessionDelta.ContainsKey("paths") -and $sessionDelta["paths"].ContainsKey("operationLog")) {
    [string]$sessionDelta["paths"]["operationLog"]
  }
  else {
    Get-ProjectOperationLogFile -Root $root -ProjectId $ProjectId -SessionId $SessionId -DatePartition (Get-Date -Format "yyyy-MM-dd")
  }
  $conflictLogPath = if ($sessionDelta.ContainsKey("paths") -and $sessionDelta["paths"].ContainsKey("conflictLog")) {
    [string]$sessionDelta["paths"]["conflictLog"]
  }
  else {
    Get-ProjectConflictLogFile -Root $root -ProjectId $ProjectId -SessionId $SessionId -DatePartition (Get-Date -Format "yyyy-MM-dd")
  }

  $record = @{
    timestamp = Get-NowIso
    operation_id = $operationIdValue
    project_id = $ProjectId
    session_id = $SessionId
    task_id = $TaskId
    agent_id = $AgentId
    action_type = $ActionType
    target_path = $targetRelativePath
    delta_summary = $DeltaSummary
    status = $Status
  }

  if ($DryRun) {
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      operationId = $operationIdValue
      sessionDeltaPath = $sessionDeltaPath
      operationLogPath = $operationLogPath
      conflictLogPath = $conflictLogPath
      record = $record
    }
    exit 0
  }

  if (Test-Path -LiteralPath $operationLogPath) {
    $duplicate = Select-String -Path $operationLogPath -SimpleMatch ('"operation_id":"{0}"' -f $operationIdValue) -Quiet
    if ($duplicate) {
      Write-JsonResult @{
        ok = $true
        dryRun = $false
        duplicateIgnored = $true
        operationId = $operationIdValue
        operationLogPath = $operationLogPath
      }
      exit 0
    }
  }

  $writeLikeActions = @("write", "modify", "delete", "patch")
  $normalizedAction = $ActionType.ToLowerInvariant()
  $registry = Read-ProjectRegistry -Root $root -ProjectId $ProjectId
  $conflictLock = $null
  if ($writeLikeActions -contains $normalizedAction) {
    foreach ($lockEntry in ($registry["locks"] | ForEach-Object { $_ })) {
      if ($lockEntry["status"] -ne "active") {
        continue
      }
      if ($lockEntry["sessionId"] -eq $SessionId) {
        continue
      }
      $scope = @()
      if ($lockEntry.ContainsKey("scope") -and $null -ne $lockEntry["scope"]) {
        $scope = @($lockEntry["scope"] | ForEach-Object { $_ })
      }
      if ((@($scope).Count -gt 0) -and (Test-PathCoveredByScope -TargetPath $targetRelativePath -Scope $scope)) {
        $conflictLock = $lockEntry
        break
      }
    }
  }

  if ($null -ne $conflictLock) {
    $conflictRecord = @{
      timestamp = Get-NowIso
      operation_id = $operationIdValue
      project_id = $ProjectId
      session_id = $SessionId
      task_id = $TaskId
      agent_id = $AgentId
      action_type = "conflict"
      target_path = $targetRelativePath
      delta_summary = "Write blocked by active scope owned by task $($conflictLock["taskId"])."
      status = "conflict"
      metadata = @{
        conflictingTaskId = $conflictLock["taskId"]
        conflictingSessionId = $conflictLock["sessionId"]
        conflictingOwner = $conflictLock["owner"]
      }
    }
    Append-JsonLine -Path $conflictLogPath -Object $conflictRecord
    if (-not $sessionDelta.ContainsKey("delta") -or -not ($sessionDelta["delta"] -is [hashtable])) {
      $sessionDelta["delta"] = @{
        summary = ""
        touchedFiles = @()
        pendingWrites = @()
        conflicts = @()
      }
    }
    $sessionDelta["delta"]["conflicts"] = @($sessionDelta["delta"]["conflicts"] | ForEach-Object { $_ }) + @(@{
      targetPath = $targetRelativePath
      conflictingTaskId = $conflictLock["taskId"]
      conflictingSessionId = $conflictLock["sessionId"]
      loggedAt = $conflictRecord["timestamp"]
    })
    $sessionDelta["updatedAt"] = $conflictRecord["timestamp"]
    $sessionDelta["lastOperation"] = $conflictRecord
    Write-JsonFile -Path $sessionDeltaPath -Object $sessionDelta
    Append-ConflictMarkdown -Root $root -ProjectId $ProjectId -Message ("task={0} session={1} agent={2} path={3} blocked_by_task={4}" -f $TaskId, $SessionId, $AgentId, $targetRelativePath, $conflictLock["taskId"])
    $triadResult = Write-TriadRecord -Root $root -ProjectId $ProjectId -SessionId $SessionId -AgentId $AgentId -TriadType "EXEC" -Content ("{0} {1} blocked: {2}" -f $ActionType, $targetRelativePath, $DeltaSummary) -RelatedFiles @($targetRelativePath) -Status "conflict" -TaskId $TaskId -Metadata @{
      actionType = $ActionType
      operationId = $operationIdValue
      targetPath = $targetRelativePath
      conflictTaskId = [string]$conflictLock["taskId"]
      conflictSessionId = [string]$conflictLock["sessionId"]
      conflictOwner = [string]$conflictLock["owner"]
      operationLogPath = $operationLogPath
      conflictLogPath = $conflictLogPath
      source = "coord-log-operation"
    }

    Write-JsonResult @{
      ok = $false
      dryRun = $false
      blocked = $true
      reason = "active_scope_conflict"
      operationId = $operationIdValue
      targetPath = $targetRelativePath
      conflictingTaskId = $conflictLock["taskId"]
      conflictingSessionId = $conflictLock["sessionId"]
      conflictLogPath = $conflictLogPath
      triadFile = $triadResult["triadFile"]
      liveStateBoardFile = $triadResult["liveStateBoardFile"]
    }
    exit 1
  }

  Append-JsonLine -Path $operationLogPath -Object $record

  if (-not $sessionDelta.ContainsKey("delta") -or -not ($sessionDelta["delta"] -is [hashtable])) {
    $sessionDelta["delta"] = @{
      summary = ""
      touchedFiles = @()
      pendingWrites = @()
      conflicts = @()
    }
  }
  if (-not $sessionDelta.ContainsKey("counters") -or -not ($sessionDelta["counters"] -is [hashtable])) {
    $sessionDelta["counters"] = @{
      read = 0
      write = 0
      modify = 0
      delete = 0
      patch = 0
      other = 0
    }
  }

  $counterKey = if ($sessionDelta["counters"].ContainsKey($normalizedAction)) { $normalizedAction } else { "other" }
  $sessionDelta["counters"][$counterKey] = [int]$sessionDelta["counters"][$counterKey] + 1
  $sessionDelta["updatedAt"] = $record["timestamp"]
  $sessionDelta["lastOperation"] = $record
  $sessionDelta["delta"]["summary"] = $DeltaSummary

  $touchedFiles = @($sessionDelta["delta"]["touchedFiles"] | ForEach-Object { $_ })
  $pendingWrites = @($sessionDelta["delta"]["pendingWrites"] | ForEach-Object { $_ })
  if (($normalizedAction -ne "other") -and ($touchedFiles -notcontains $targetRelativePath)) {
    $touchedFiles += $targetRelativePath
  }
  if ((@("write", "modify", "delete", "patch") -contains $normalizedAction) -and ($pendingWrites -notcontains $targetRelativePath)) {
    $pendingWrites += $targetRelativePath
  }
  $sessionDelta["delta"]["touchedFiles"] = ,@($touchedFiles)
  $sessionDelta["delta"]["pendingWrites"] = ,@($pendingWrites)
  Write-JsonFile -Path $sessionDeltaPath -Object $sessionDelta

  $l1EntryFile = Get-ProjectL1EntryFile -Root $root -ProjectId $ProjectId -SessionId $SessionId
  if (Test-Path -LiteralPath $l1EntryFile) {
    $l1Entry = Read-JsonFile -Path $l1EntryFile
    $l1Entry["updatedAt"] = $record["timestamp"]
    $l1Entry["lastActionType"] = $ActionType
    $l1Entry["lastTargetPath"] = $targetRelativePath
    $l1Entry["deltaSummary"] = $DeltaSummary
    Write-JsonFile -Path $l1EntryFile -Object $l1Entry
  }

  $activeIndexFile = Get-ProjectActiveSessionsIndexFile -Root $root -ProjectId $ProjectId
  if (Test-Path -LiteralPath $activeIndexFile) {
    $activeIndex = Read-JsonFile -Path $activeIndexFile
    if ($activeIndex.ContainsKey("sessions")) {
      $activeIndex["sessions"] = Upsert-ListEntry -Items $activeIndex["sessions"] -Item @{
        sessionId = $SessionId
        taskId = $TaskId
        agentId = $AgentId
        status = if ($sessionDelta.ContainsKey("status")) { [string]$sessionDelta["status"] } else { "active" }
        sessionDeltaFile = $sessionDeltaPath
        updatedAt = $record["timestamp"]
      } -Key "sessionId"
      $activeIndex["updatedAt"] = $record["timestamp"]
      Write-JsonFile -Path $activeIndexFile -Object $activeIndex
    }
  }

  $registry["sessions"] = Upsert-ListEntry -Items $registry["sessions"] -Item @{
    sessionId = $SessionId
    taskId = $TaskId
    owner = $AgentId
    status = if ($sessionDelta.ContainsKey("status")) { [string]$sessionDelta["status"] } else { "active" }
    sessionDeltaFile = $sessionDeltaPath
    operationLogPath = $operationLogPath
    updatedAt = $record["timestamp"]
  } -Key "sessionId"
  Write-ProjectRegistry -Root $root -ProjectId $ProjectId -Registry $registry
  $triadResult = Write-TriadRecord -Root $root -ProjectId $ProjectId -SessionId $SessionId -AgentId $AgentId -TriadType "EXEC" -Content ("{0} {1}: {2}" -f $ActionType, $targetRelativePath, $DeltaSummary) -RelatedFiles @($targetRelativePath) -Status $Status -TaskId $TaskId -Metadata @{
    actionType = $ActionType
    operationId = $operationIdValue
    targetPath = $targetRelativePath
    operationLogPath = $operationLogPath
    conflictLogPath = $conflictLogPath
    source = "coord-log-operation"
  }
  Update-CoordinationRuntimeIndexes -Root $root

  Write-JsonResult @{
    ok = $true
    dryRun = $false
    operationId = $operationIdValue
    targetPath = $targetRelativePath
    status = $Status
    operationLogPath = $operationLogPath
    sessionDeltaPath = $sessionDeltaPath
    triadFile = $triadResult["triadFile"]
    newestEntryFile = $triadResult["newestEntryFile"]
    liveStateBoardFile = $triadResult["liveStateBoardFile"]
    activeContextFile = (Get-CoordinationActiveContextMarkdownFile -Root $root)
    aiStartFile = (Get-ProjectAiStartFile -Root $root -ProjectId $ProjectId)
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
