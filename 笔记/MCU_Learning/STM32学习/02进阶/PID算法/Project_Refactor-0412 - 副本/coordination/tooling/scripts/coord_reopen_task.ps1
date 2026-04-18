[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectId,
  [Parameter(Mandatory = $true)]
  [string]$AgentName,
  [Parameter(Mandatory = $true)]
  [string]$TaskId,
  [string]$Goal,
  [string]$SessionId,
  [string]$BaseBranch,
  [string]$BranchName,
  [string]$WorktreeName,
  [string[]]$Scope = @(),
  [string]$ReopenReason,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

function Invoke-JsonScript {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ScriptPath,
    [string[]]$Arguments = @()
  )

  $text = (& powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptPath @Arguments | Out-String).Trim()
  $exitCode = $LASTEXITCODE
  $json = if ([string]::IsNullOrWhiteSpace($text)) { $null } else { ConvertTo-PlainObject ($text | ConvertFrom-Json) }
  return [pscustomobject]@{
    exitCode = $exitCode
    json = $json
    raw = $text
  }
}

function Update-TaskMarkdown {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [string]$Goal,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [Parameter(Mandatory = $true)]
    [string]$AgentName,
    [Parameter(Mandatory = $true)]
    [string]$BranchName,
    [Parameter(Mandatory = $true)]
    [string]$WorktreePath,
    [Parameter(Mandatory = $true)]
    [string]$ScopeText,
    [Parameter(Mandatory = $true)]
    [string]$NowIso,
    [string]$ReopenReason
  )

  $content = Get-Content -LiteralPath $Path -Raw
  $content = [regex]::Replace($content, '(?m)^session_id:\s*.*$', "session_id: $SessionId")
  $content = [regex]::Replace($content, '(?m)^owner:\s*.*$', "owner: $AgentName")
  $content = [regex]::Replace($content, '(?m)^status:\s*.*$', "status: InProgress")
  $content = [regex]::Replace($content, '(?m)^last_updated:\s*.*$', "last_updated: $NowIso")
  $content = [regex]::Replace($content, '(?m)^- Owner: .+$', "- Owner: $AgentName")
  $content = [regex]::Replace($content, '(?m)^- Session: .+$', "- Session: $SessionId")
  $content = [regex]::Replace($content, '(?m)^- Branch: .+$', "- Branch: $BranchName")
  $content = [regex]::Replace($content, '(?m)^- Worktree: .+$', "- Worktree: $WorktreePath")
  $content = [regex]::Replace($content, '(?m)^- Scope: .+$', "- Scope: $ScopeText")
  $content = [regex]::Replace($content, '(?m)^- Status: `[^`]+`$', "- Status: ``In Progress``")
  $content = [regex]::Replace($content, '(?m)^- Updated at: .+$', "- Updated at: " + (Get-Date -Format "yyyy-MM-dd"))
  if (-not [string]::IsNullOrWhiteSpace($Goal) -and $content.Contains("- Describe the concrete outcome.")) {
    $content = $content.Replace("- Describe the concrete outcome.", "- $Goal")
  }

  $reasonSuffix = if ([string]::IsNullOrWhiteSpace($ReopenReason)) { "" } else { " Reason: $ReopenReason" }
  $note = "- Reopened at {0} by {1} session={2}.{3}" -f (Get-Date -Format "yyyy-MM-dd"), $AgentName, $SessionId, $reasonSuffix
  if ($content.Contains("## Files") -and ($content -notmatch [regex]::Escape($note))) {
    $content = $content.Replace("## Files", $note + [Environment]::NewLine + [Environment]::NewLine + "## Files")
  }

  Write-TextFile -Path $Path -Content $content
}

function Update-BoardFile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    [string]$TaskId,
    [Parameter(Mandatory = $true)]
    [string]$Entry
  )

  $lines = [System.Collections.Generic.List[string]]::new()
  foreach ($line in (Get-Content -LiteralPath $Path)) {
    if ($line -notmatch [regex]::Escape($TaskId)) {
      [void]$lines.Add($line)
    }
  }

  $insertIndex = $lines.IndexOf("## Active")
  if ($insertIndex -ge 0) {
    $lines.Insert($insertIndex + 1, $Entry)
  }
  else {
    [void]$lines.Add("")
    [void]$lines.Add("## Active")
    [void]$lines.Add($Entry)
  }

  Write-TextFile -Path $Path -Content ([string]::Join([Environment]::NewLine, $lines))
}

function Update-HandoffSnapshot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [string]$PreviousOwner,
    [Parameter(Mandatory = $true)]
    [string]$AgentName,
    [Parameter(Mandatory = $true)]
    [string]$TaskId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [Parameter(Mandatory = $true)]
    [string]$BranchName,
    [Parameter(Mandatory = $true)]
    [string]$WorktreePath,
    [string]$ReopenReason
  )

  $content = Get-Content -LiteralPath $Path -Raw
  $latestBlock = @"
## Latest

- Time: $(Get-Date -Format "yyyy-MM-dd")
- From: $(if ([string]::IsNullOrWhiteSpace($PreviousOwner)) { "operator" } else { $PreviousOwner })
- To: $AgentName
- Session: $SessionId
- Task: $TaskId
- Status: reopened
- Branch: $BranchName
- Worktree: $WorktreePath
- Files touched:
- Next step: $(if ([string]::IsNullOrWhiteSpace($ReopenReason)) { "Read the generated AI prompt and continue the task." } else { $ReopenReason })
- Risk or blocker:
"@
  $content = [regex]::Replace($content, '(?s)## Latest\s*.*?\s*## Rule', $latestBlock + [Environment]::NewLine + [Environment]::NewLine + '## Rule')
  Write-TextFile -Path $Path -Content $content
}

try {
  if ($TaskId -notmatch "^[A-Za-z0-9._-]+$") {
    throw "TaskId must match ^[A-Za-z0-9._-]+$."
  }

  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $inputScope = ConvertTo-StringArray -Value $Scope
  $project = Read-ProjectConfig -Root $root -ProjectId $ProjectId
  $repoRoot = Resolve-GitRepositoryRoot -Path ([string]$project["repoPath"])
  $registry = Read-ProjectRegistry -Root $root -ProjectId $ProjectId
  $tasksPath = Get-ProjectTasksPath -Root $root -ProjectId $ProjectId
  $locksPath = Get-ProjectLocksPath -Root $root -ProjectId $ProjectId

  $taskFile = Join-Path $tasksPath ($TaskId + ".md")
  $lockFile = Join-Path $locksPath ($TaskId + ".json")
  if (-not (Test-Path -LiteralPath $taskFile)) {
    throw "Task file not found: $taskFile. Use coord-claim-task for a brand new task."
  }

  $taskEntry = $null
  foreach ($entry in ($registry["tasks"] | ForEach-Object { $_ })) {
    if ($entry["taskId"] -eq $TaskId) {
      $taskEntry = $entry
      break
    }
  }

  $lock = if (Test-Path -LiteralPath $lockFile) { Read-JsonFile -Path $lockFile } else { @{} }
  $previousOwner = if ($lock.ContainsKey("owner")) { [string]$lock["owner"] } elseif ($null -ne $taskEntry -and $taskEntry.ContainsKey("owner")) { [string]$taskEntry["owner"] } else { "" }
  $previousSessionId = if ($lock.ContainsKey("sessionId")) { [string]$lock["sessionId"] } elseif ($null -ne $taskEntry -and $taskEntry.ContainsKey("sessionId")) { [string]$taskEntry["sessionId"] } else { "" }

  $taskIsActive = $false
  if (($null -ne $taskEntry) -and ($taskEntry.ContainsKey("status")) -and ([string]$taskEntry["status"]).ToLowerInvariant() -eq "active") {
    $taskIsActive = $true
  }
  if ($lock.ContainsKey("status") -and ([string]$lock["status"]).ToLowerInvariant() -eq "active") {
    $taskIsActive = $true
  }
  foreach ($entry in ($registry["tiers"]["l1Active"] | ForEach-Object { $_ })) {
    if ($entry["taskId"] -eq $TaskId) {
      $taskIsActive = $true
      break
    }
  }
  if ($taskIsActive) {
    throw "Task $TaskId is already active. Use coord-handoff-task for a baton change or coord-close-task before reopening."
  }

  $scopeSource = if ($inputScope.Count -gt 0) { $inputScope } elseif ($lock.ContainsKey("scope") -and $null -ne $lock["scope"]) { $lock["scope"] } else { @() }
  $resolvedScope = ConvertTo-StringArray -Value $scopeSource
  if ($resolvedScope.Count -gt 0) {
    foreach ($existingLock in ($registry["locks"] | ForEach-Object { $_ })) {
      if ($existingLock["taskId"] -eq $TaskId) {
        continue
      }
      if ($existingLock["status"] -ne "active") {
        continue
      }
      $existingScope = @()
      if ($existingLock.ContainsKey("scope") -and $null -ne $existingLock["scope"]) {
        $existingScope = @($existingLock["scope"] | ForEach-Object { $_ })
      }
      if ((@($existingScope).Count -gt 0) -and (Test-ScopeOverlap -Left $resolvedScope -Right $existingScope)) {
        throw "Scope overlaps with active task $($existingLock["taskId"]) owned by $($existingLock["owner"]). Use STAGING\\patches\\ or choose a non-overlapping scope."
      }
    }
  }

  $agentSlug = ConvertTo-Slug -Text $AgentName
  $taskSlug = ConvertTo-Slug -Text $TaskId
  $resolvedSessionId = if (-not [string]::IsNullOrWhiteSpace($SessionId)) { $SessionId } else { New-SessionId -AgentName $AgentName -TaskId $TaskId }
  $resolvedBranchName = if (-not [string]::IsNullOrWhiteSpace($BranchName)) { $BranchName } elseif ($lock.ContainsKey("branch")) { [string]$lock["branch"] } elseif ($null -ne $taskEntry -and $taskEntry.ContainsKey("branch")) { [string]$taskEntry["branch"] } else { "ai/$agentSlug/$taskSlug" }
  $resolvedBaseBranch = if (-not [string]::IsNullOrWhiteSpace($BaseBranch)) { $BaseBranch } elseif ($null -ne $taskEntry -and $taskEntry.ContainsKey("baseBranch")) { [string]$taskEntry["baseBranch"] } else { [string]$project["defaultBranch"] }
  $resolvedWorktreeName = if (-not [string]::IsNullOrWhiteSpace($WorktreeName)) { $WorktreeName } else { "$agentSlug-$taskSlug" }
  $worktreePath = Join-Path ([string]$project["worktreeRoot"]) $resolvedWorktreeName

  if ((Test-Path -LiteralPath $worktreePath) -and -not $DryRun) {
    throw "Worktree path already exists: $worktreePath"
  }

  $sessionDir = Get-ProjectSessionDirectory -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "active"
  $sessionDeltaFile = Get-ProjectSessionDeltaFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "active"
  $thoughtLogFile = Get-ProjectSessionTriadFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -TriadType "THOUGHT" -State "active"
  $execLogFile = Get-ProjectSessionTriadFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -TriadType "EXEC" -State "active"
  $modLogFile = Get-ProjectSessionTriadFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -TriadType "MOD" -State "active"
  $l1EntryFile = Get-ProjectL1EntryFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId
  $datePartition = Get-Date -Format "yyyy-MM-dd"
  $operationLogPath = Get-ProjectOperationLogFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -DatePartition $datePartition
  $conflictLogPath = Get-ProjectConflictLogFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -DatePartition $datePartition
  $promptFile = Get-ProjectSessionPromptFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "active"
  $streamNewestEntryFile = Get-ProjectStreamNewestEntryFile -Root $root -ProjectId $ProjectId
  $streamTailFile = Get-ProjectStreamTailFile -Root $root -ProjectId $ProjectId
  $readPointerDir = Get-ProjectStreamReadPointersPath -Root $root -ProjectId $ProjectId
  $liveStateBoardFile = Get-ProjectLiveStateBoardFile -Root $root -ProjectId $ProjectId
  $digestLatestSummaryFile = Get-ProjectDigestLatestSummaryFile -Root $root -ProjectId $ProjectId
  $conflictMarkdownFile = Get-ProjectConflictMarkdownFile -Root $root -ProjectId $ProjectId

  $baseWarning = $null
  $baseRef = if (Test-GitRef -RepoPath $repoRoot -RefName $resolvedBaseBranch) {
    $resolvedBaseBranch
  }
  elseif (Test-GitRef -RepoPath $repoRoot -RefName ("origin/" + $resolvedBaseBranch)) {
    "origin/" + $resolvedBaseBranch
  }
  elseif ($DryRun) {
    $baseWarning = "Base branch was not found in this repo. Dry-run kept the requested branch name for preview only."
    $resolvedBaseBranch
  }
  else {
    throw "Base branch not found locally or on origin: $resolvedBaseBranch"
  }

  $branchExists = Test-GitRef -RepoPath $repoRoot -RefName ("refs/heads/" + $resolvedBranchName)
  $gitArgs = if ($branchExists) {
    @("worktree", "add", $worktreePath, $resolvedBranchName)
  }
  else {
    @("worktree", "add", $worktreePath, "-b", $resolvedBranchName, $baseRef)
  }

  $now = Get-NowIso
  $scopeText = if ($resolvedScope.Count -gt 0) { [string]::Join(", ", $resolvedScope) } else { "not set" }
  $baselineVersion = if ($registry.ContainsKey("baselineVersion")) { [string]$registry["baselineVersion"] } else { [string]$project["baselineVersion"] }
  $tokens = @{
    PROJECT_ID = $ProjectId
    TASK_ID = $TaskId
    SESSION_ID = $resolvedSessionId
    TODAY = (Get-Date -Format "yyyy-MM-dd")
    OWNER = $AgentName
    BRANCH_NAME = $resolvedBranchName
    WORKTREE_PATH = $worktreePath
    WORKTREE_PATH_JSON = $worktreePath.Replace("\", "\\")
    SCOPE = $scopeText
    NOW = $now
    BASELINE_VERSION = $baselineVersion
    TASK_FILE_PATH_JSON = $taskFile.Replace("\", "\\")
    LOCK_FILE_PATH_JSON = $lockFile.Replace("\", "\\")
    L1_ENTRY_PATH_JSON = $l1EntryFile.Replace("\", "\\")
    OPERATION_LOG_PATH_JSON = $operationLogPath.Replace("\", "\\")
    CONFLICT_LOG_PATH_JSON = $conflictLogPath.Replace("\", "\\")
  }

  if ($DryRun) {
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      projectId = $ProjectId
      taskId = $TaskId
      sessionId = $resolvedSessionId
      previousSessionId = $previousSessionId
      agentName = $AgentName
      branchName = $resolvedBranchName
      baseBranch = $resolvedBaseBranch
      worktreePath = $worktreePath
      taskFile = $taskFile
      lockFile = $lockFile
      sessionDeltaFile = $sessionDeltaFile
      promptFile = $promptFile
      aiStartFile = (Get-ProjectAiStartFile -Root $root -ProjectId $ProjectId)
      activeContextFile = (Get-CoordinationActiveContextMarkdownFile -Root $root)
      scope = $resolvedScope
      warning = $baseWarning
      command = (Format-CommandLine -FilePath (Get-GitCommand) -Arguments (@("-C", $repoRoot) + $gitArgs))
    }
    exit 0
  }

  foreach ($path in @(
    [string]$project["worktreeRoot"],
    $tasksPath,
    $locksPath,
    $sessionDir,
    $readPointerDir,
    (Split-Path -Parent $operationLogPath),
    (Split-Path -Parent $conflictLogPath)
  )) {
    Ensure-Directory -Path $path
  }

  $gitResult = Invoke-GitCapture -RepoPath $repoRoot -Arguments $gitArgs
  if (-not $gitResult.ok) {
    throw ("git worktree add failed: " + $gitResult.stderr)
  }

  Update-TaskMarkdown -Path $taskFile -Goal $Goal -SessionId $resolvedSessionId -AgentName $AgentName -BranchName $resolvedBranchName -WorktreePath $worktreePath -ScopeText $scopeText -NowIso $now -ReopenReason $ReopenReason

  $lockObject = @{
    taskId = $TaskId
    sessionId = $resolvedSessionId
    owner = $AgentName
    branch = $resolvedBranchName
    worktreePath = $worktreePath
    status = "active"
    scope = $resolvedScope
    claimedAt = $now
    reopenedAt = $now
    updatedAt = $now
    operationLogPath = $operationLogPath
    sessionDeltaPath = $sessionDeltaFile
    notes = if ([string]::IsNullOrWhiteSpace($ReopenReason)) { "" } else { $ReopenReason }
  }
  Write-JsonFile -Path $lockFile -Object $lockObject

  $sessionTemplate = Apply-Template -Template (Read-TemplateFile -Root $root -RelativePath "SESSIONS\SESSION_DELTA.json") -Tokens $tokens
  Write-TextFile -Path $sessionDeltaFile -Content ($sessionTemplate + [Environment]::NewLine)
  Ensure-SessionTriadFiles -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "active"

  $sessionDelta = Read-JsonFile -Path $sessionDeltaFile
  if (-not $sessionDelta.ContainsKey("paths") -or -not ($sessionDelta["paths"] -is [hashtable])) {
    $sessionDelta["paths"] = @{}
  }
  $sessionDelta["paths"]["thoughtLog"] = $thoughtLogFile
  $sessionDelta["paths"]["execLog"] = $execLogFile
  $sessionDelta["paths"]["modLog"] = $modLogFile
  $sessionDelta["paths"]["streamEntries"] = (Get-ProjectStreamEntriesFile -Root $root -ProjectId $ProjectId)
  $sessionDelta["paths"]["streamNewestEntry"] = $streamNewestEntryFile
  $sessionDelta["paths"]["streamTail"] = $streamTailFile
  $sessionDelta["paths"]["readPointerDir"] = $readPointerDir
  $sessionDelta["paths"]["liveStateBoard"] = $liveStateBoardFile
  $sessionDelta["paths"]["digestLatestSummary"] = $digestLatestSummaryFile
  $sessionDelta["paths"]["stateEventLog"] = (Get-ProjectStateEventLogFile -Root $root -ProjectId $ProjectId)
  $sessionDelta["paths"]["transactionsRoot"] = (Get-ProjectTransactionsPath -Root $root -ProjectId $ProjectId)
  $sessionDelta["paths"]["conflictMarkdown"] = $conflictMarkdownFile
  Write-JsonFile -Path $sessionDeltaFile -Object $sessionDelta

  $l1Entry = @{
    version = 1
    sessionId = $resolvedSessionId
    taskId = $TaskId
    agentId = $AgentName
    branch = $resolvedBranchName
    worktreePath = $worktreePath
    scope = $resolvedScope
    status = "active"
    taskFile = $taskFile
    lockFile = $lockFile
    sessionDeltaFile = $sessionDeltaFile
    thoughtLogFile = $thoughtLogFile
    execLogFile = $execLogFile
    modLogFile = $modLogFile
    liveStateBoardFile = $liveStateBoardFile
    digestLatestSummaryFile = $digestLatestSummaryFile
    operationLogPath = $operationLogPath
    deltaSummary = if ([string]::IsNullOrWhiteSpace($ReopenReason)) { "Task reopened." } else { $ReopenReason }
    updatedAt = $now
  }
  Write-JsonFile -Path $l1EntryFile -Object $l1Entry

  $activeSessionsIndexFile = Get-ProjectActiveSessionsIndexFile -Root $root -ProjectId $ProjectId
  $activeSessionsIndex = Read-JsonFile -Path $activeSessionsIndexFile
  if (-not $activeSessionsIndex.ContainsKey("sessions")) {
    $activeSessionsIndex = @{
      version = 1
      updatedAt = $now
      sessions = @()
    }
  }
  $activeSessionsIndex["sessions"] = Upsert-ListEntry -Items $activeSessionsIndex["sessions"] -Item @{
    sessionId = $resolvedSessionId
    taskId = $TaskId
    agentId = $AgentName
    status = "active"
    branch = $resolvedBranchName
    worktreePath = $worktreePath
    sessionDeltaFile = $sessionDeltaFile
    updatedAt = $now
  } -Key "sessionId"
  $activeSessionsIndex["updatedAt"] = $now
  Write-JsonFile -Path $activeSessionsIndexFile -Object $activeSessionsIndex

  Append-JsonLine -Path $operationLogPath -Object @{
    timestamp = $now
    operation_id = "OP-{0}-{1}" -f (Get-Date -Format "yyyyMMddHHmmss"), "task-reopen"
    project_id = $ProjectId
    session_id = $resolvedSessionId
    task_id = $TaskId
    agent_id = $AgentName
    action_type = "task_reopen"
    target_path = ($worktreePath -replace "\\", "/")
    delta_summary = if ([string]::IsNullOrWhiteSpace($ReopenReason)) { "Reopened task and recreated active session state." } else { $ReopenReason }
    status = "completed"
  }

  Update-BoardFile -Path (Get-ProjectBoardFile -Root $root -ProjectId $ProjectId) -TaskId $TaskId -Entry ("- `[In Progress]` {0} owner={1} session={2} scope={3}" -f $TaskId, $AgentName, $resolvedSessionId, $scopeText)
  Update-HandoffSnapshot -Path (Get-ProjectHandoffFile -Root $root -ProjectId $ProjectId) -PreviousOwner $previousOwner -AgentName $AgentName -TaskId $TaskId -SessionId $resolvedSessionId -BranchName $resolvedBranchName -WorktreePath $worktreePath -ReopenReason $ReopenReason

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
    previousSessionId = $previousSessionId
    owner = $AgentName
    branch = $resolvedBranchName
    worktreePath = $worktreePath
    baseBranch = $resolvedBaseBranch
    taskFile = $taskFile
    lockFile = $lockFile
    status = "active"
    reopenedAt = $now
    updatedAt = $now
  } -Key "taskId"
  $registry["locks"] = Upsert-ListEntry -Items $registry["locks"] -Item @{
    taskId = $TaskId
    sessionId = $resolvedSessionId
    owner = $AgentName
    branch = $resolvedBranchName
    worktreePath = $worktreePath
    scope = $resolvedScope
    status = "active"
    reopenedAt = $now
    updatedAt = $now
  } -Key "taskId"
  $registry["sessions"] = Upsert-ListEntry -Items $registry["sessions"] -Item @{
    sessionId = $resolvedSessionId
    taskId = $TaskId
    owner = $AgentName
    branch = $resolvedBranchName
    worktreePath = $worktreePath
    taskFile = $taskFile
    lockFile = $lockFile
    sessionDeltaFile = $sessionDeltaFile
    operationLogPath = $operationLogPath
    l1EntryFile = $l1EntryFile
    status = "active"
    startedAt = $now
    reopenedAt = $now
    updatedAt = $now
  } -Key "sessionId"
  $registry["tiers"]["l1Active"] = Upsert-ListEntry -Items $registry["tiers"]["l1Active"] -Item @{
    sessionId = $resolvedSessionId
    taskId = $TaskId
    status = "active"
    file = $l1EntryFile
    updatedAt = $now
  } -Key "sessionId"
  Write-ProjectRegistry -Root $root -ProjectId $ProjectId -Registry $registry

  $project["updatedAt"] = $now
  Write-JsonFile -Path (Get-ProjectFile -Root $root -ProjectId $ProjectId) -Object $project
  Upsert-CoordinationProject -Root $root -Project $project

  $promptArgs = @(
    "-ProjectId", $ProjectId,
    "-TaskId", $TaskId,
    "-SessionId", $resolvedSessionId,
    "-AgentName", $AgentName
  )
  $promptResult = Invoke-JsonScript -ScriptPath (Join-Path $PSScriptRoot "coord_generate_prompt.ps1") -Arguments $promptArgs
  if ($promptResult.exitCode -ne 0) {
    throw $promptResult.json.error
  }

  Write-JsonResult @{
    ok = $true
    dryRun = $false
    projectId = $ProjectId
    taskId = $TaskId
    sessionId = $resolvedSessionId
    previousSessionId = $previousSessionId
    agentName = $AgentName
    branchName = $resolvedBranchName
    baseBranch = $resolvedBaseBranch
    worktreePath = $worktreePath
    taskFile = $taskFile
    lockFile = $lockFile
    sessionDeltaFile = $sessionDeltaFile
    aiStartFile = (Get-ProjectAiStartFile -Root $root -ProjectId $ProjectId)
    activeContextFile = (Get-CoordinationActiveContextMarkdownFile -Root $root)
    promptFile = $promptResult.json.promptFile
    nextStep = "Send the generated prompt file to the assigned AI, then resume from AI_START -> LIVE_STATE_BOARD/L3_DIGEST and continue THOUGHT/EXEC/MOD logging in the new worktree."
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
