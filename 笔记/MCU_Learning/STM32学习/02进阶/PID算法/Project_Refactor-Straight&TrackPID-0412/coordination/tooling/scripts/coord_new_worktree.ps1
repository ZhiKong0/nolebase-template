[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectId,
  [Parameter(Mandatory = $true)]
  [string]$AgentName,
  [Parameter(Mandatory = $true)]
  [string]$TaskId,
  [string]$SessionId,
  [string]$BaseBranch,
  [string]$BranchName,
  [string]$WorktreeName,
  [string[]]$Scope = @(),
  [switch]$Force,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

try {
  if ($TaskId -notmatch "^[A-Za-z0-9._-]+$") {
    throw "TaskId must match ^[A-Za-z0-9._-]+$."
  }

  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $normalizedScope = ConvertTo-StringArray -Value $Scope
  $project = Read-ProjectConfig -Root $root -ProjectId $ProjectId
  $tasksPath = Get-ProjectTasksPath -Root $root -ProjectId $ProjectId
  $locksPath = Get-ProjectLocksPath -Root $root -ProjectId $ProjectId
  $repoRoot = Resolve-GitRepositoryRoot -Path ([string]$project["repoPath"])
  $worktreeRoot = [string]$project["worktreeRoot"]
  $registry = Read-ProjectRegistry -Root $root -ProjectId $ProjectId
  $baselineVersion = [string]$registry["baselineVersion"]
  if ([string]::IsNullOrWhiteSpace($baselineVersion)) {
    $baselineVersion = [string]$project["baselineVersion"]
  }
  $resolvedBaseBranch = if (-not [string]::IsNullOrWhiteSpace($BaseBranch)) { $BaseBranch } else { [string]$project["defaultBranch"] }

  $agentSlug = ConvertTo-Slug -Text $AgentName
  $taskSlug = ConvertTo-Slug -Text $TaskId
  $resolvedSessionId = if (-not [string]::IsNullOrWhiteSpace($SessionId)) { $SessionId } else { New-SessionId -AgentName $AgentName -TaskId $TaskId }
  $resolvedBranchName = if (-not [string]::IsNullOrWhiteSpace($BranchName)) { $BranchName } else { "ai/$agentSlug/$taskSlug" }
  $resolvedWorktreeName = if (-not [string]::IsNullOrWhiteSpace($WorktreeName)) { $WorktreeName } else { "$agentSlug-$taskSlug" }
  $worktreePath = Join-Path $worktreeRoot $resolvedWorktreeName

  $taskFile = Join-Path $tasksPath ($TaskId + ".md")
  $lockFile = Join-Path $locksPath ($TaskId + ".json")
  $sessionDir = Get-ProjectSessionDirectory -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "active"
  $sessionDeltaFile = Get-ProjectSessionDeltaFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "active"
  $thoughtLogFile = Get-ProjectSessionTriadFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -TriadType "THOUGHT" -State "active"
  $execLogFile = Get-ProjectSessionTriadFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -TriadType "EXEC" -State "active"
  $modLogFile = Get-ProjectSessionTriadFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -TriadType "MOD" -State "active"
  $l1EntryFile = Get-ProjectL1EntryFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId
  $datePartition = Get-Date -Format "yyyy-MM-dd"
  $operationLogPath = Get-ProjectOperationLogFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -DatePartition $datePartition
  $conflictLogPath = Get-ProjectConflictLogFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -DatePartition $datePartition
  $streamNewestEntryFile = Get-ProjectStreamNewestEntryFile -Root $root -ProjectId $ProjectId
  $streamTailFile = Get-ProjectStreamTailFile -Root $root -ProjectId $ProjectId
  $readPointerDir = Get-ProjectStreamReadPointersPath -Root $root -ProjectId $ProjectId
  $liveStateBoardFile = Get-ProjectLiveStateBoardFile -Root $root -ProjectId $ProjectId
  $digestLatestSummaryFile = Get-ProjectDigestLatestSummaryFile -Root $root -ProjectId $ProjectId
  $conflictMarkdownFile = Get-ProjectConflictMarkdownFile -Root $root -ProjectId $ProjectId

  if (((Test-Path -LiteralPath $taskFile) -or (Test-Path -LiteralPath $lockFile) -or (Test-Path -LiteralPath $worktreePath) -or (Test-Path -LiteralPath $sessionDir)) -and -not $Force) {
    throw "Task, worktree, or session already exists for $TaskId. Use -Force only when you intentionally want to overwrite coordination files."
  }

  if ($normalizedScope.Count -gt 0) {
    foreach ($existingLock in ($registry["locks"] | ForEach-Object { $_ })) {
      if ($existingLock["status"] -ne "active") {
        continue
      }
      $existingScope = @()
      if ($existingLock.ContainsKey("scope") -and $null -ne $existingLock["scope"]) {
        $existingScope = @($existingLock["scope"] | ForEach-Object { $_ })
      }
      if ((@($existingScope).Count -gt 0) -and (Test-ScopeOverlap -Left $normalizedScope -Right $existingScope)) {
        throw "Scope overlaps with active task $($existingLock["taskId"]) owned by $($existingLock["owner"]). Use STAGING\\patches\\ or choose a non-overlapping scope."
      }
    }
  }

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

  $branchExists = (Test-GitRef -RepoPath $repoRoot -RefName ("refs/heads/" + $resolvedBranchName))
  $gitArgs = if ($branchExists) {
    @("worktree", "add", $worktreePath, $resolvedBranchName)
  }
  else {
    @("worktree", "add", $worktreePath, "-b", $resolvedBranchName, $baseRef)
  }

  $now = Get-NowIso
  $scopeText = if ($normalizedScope.Count -gt 0) { [string]::Join(", ", $normalizedScope) } else { "not set yet" }
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
    $command = Format-CommandLine -FilePath (Get-GitCommand) -Arguments (@("-C", $repoRoot) + $gitArgs)
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      projectId = $ProjectId
      taskId = $TaskId
      sessionId = $resolvedSessionId
      agentName = $AgentName
      baseBranch = $resolvedBaseBranch
      branchName = $resolvedBranchName
      worktreePath = $worktreePath
      taskFile = $taskFile
      lockFile = $lockFile
      sessionDeltaFile = $sessionDeltaFile
      l1EntryFile = $l1EntryFile
      operationLogPath = $operationLogPath
      warning = $baseWarning
      command = $command
      aiStartFile = (Get-ProjectAiStartFile -Root $root -ProjectId $ProjectId)
      activeContextFile = (Get-CoordinationActiveContextMarkdownFile -Root $root)
      onboardingPrompt = (Get-ProjectOnboardingPromptFile -Root $root -ProjectId $ProjectId)
      dispatchedAt = $now
    }
    exit 0
  }

  foreach ($path in @(
    $worktreeRoot,
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

  $taskContent = Apply-Template -Template (Read-TemplateFile -Root $root -RelativePath "tasks\TASK_TEMPLATE.md") -Tokens $tokens
  Write-TextFile -Path $taskFile -Content $taskContent

  $lockObject = @{
    taskId = $TaskId
    sessionId = $resolvedSessionId
    owner = $AgentName
    branch = $resolvedBranchName
    worktreePath = $worktreePath
    status = "active"
    scope = $normalizedScope
    claimedAt = $now
    updatedAt = $now
    operationLogPath = $operationLogPath
    sessionDeltaPath = $sessionDeltaFile
    notes = ""
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
  $sessionDelta["paths"]["streamNewestEntry"] = $streamNewestEntryFile
  $sessionDelta["paths"]["streamTail"] = $streamTailFile
  $sessionDelta["paths"]["readPointerDir"] = $readPointerDir
  $sessionDelta["paths"]["liveStateBoard"] = $liveStateBoardFile
  $sessionDelta["paths"]["digestLatestSummary"] = $digestLatestSummaryFile
  $sessionDelta["paths"]["conflictMarkdown"] = $conflictMarkdownFile
  Write-JsonFile -Path $sessionDeltaFile -Object $sessionDelta

  $l1Entry = @{
    version = 1
    sessionId = $resolvedSessionId
    taskId = $TaskId
    agentId = $AgentName
    branch = $resolvedBranchName
    worktreePath = $worktreePath
    scope = $normalizedScope
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
    deltaSummary = ""
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
    operation_id = "OP-{0}-{1}" -f (Get-Date -Format "yyyyMMddHHmmss"), "session-open"
    project_id = $ProjectId
    session_id = $resolvedSessionId
    task_id = $TaskId
    agent_id = $AgentName
    action_type = "session_open"
    target_path = ($worktreePath -replace "\\", "/")
    delta_summary = "Created worktree, task file, lock file, session delta, and active changelog entry."
    status = "completed"
  }

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
    owner = $AgentName
    branch = $resolvedBranchName
    worktreePath = $worktreePath
    baseBranch = $resolvedBaseBranch
    taskFile = $taskFile
    lockFile = $lockFile
    status = "active"
    updatedAt = $now
  } -Key "taskId"
  $registry["locks"] = Upsert-ListEntry -Items $registry["locks"] -Item @{
    taskId = $TaskId
    sessionId = $resolvedSessionId
    owner = $AgentName
    branch = $resolvedBranchName
    worktreePath = $worktreePath
    scope = $normalizedScope
    status = "active"
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
  Update-CoordinationRuntimeIndexes -Root $root

  Write-JsonResult @{
    ok = $true
    dryRun = $false
    projectId = $ProjectId
    taskId = $TaskId
    sessionId = $resolvedSessionId
    agentName = $AgentName
    branchName = $resolvedBranchName
    baseBranch = $resolvedBaseBranch
    worktreePath = $worktreePath
    warning = $baseWarning
    taskFile = $taskFile
    lockFile = $lockFile
    sessionDeltaFile = $sessionDeltaFile
    operationLogPath = $operationLogPath
    aiStartFile = (Get-ProjectAiStartFile -Root $root -ProjectId $ProjectId)
    activeContextFile = (Get-CoordinationActiveContextMarkdownFile -Root $root)
    onboardingPrompt = (Get-ProjectOnboardingPromptFile -Root $root -ProjectId $ProjectId)
    nextStep = "Read AI_ENTRY -> ACTIVE_CONTEXT -> AI_START -> follow AI_START, paste the onboarding prompt into the assigned AI chat, and start THOUGHT/EXEC/MOD logging before repository reads or writes."
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
