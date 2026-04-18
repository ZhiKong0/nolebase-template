[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectId,
  [Parameter(Mandatory = $true)]
  [string]$TaskId,
  [string]$SessionId,
  [string]$AgentName,
  [string]$OutputPath,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

function Get-TaskGoalSummary {
  param([string]$TaskContent)

  $match = [regex]::Match($TaskContent, '(?ms)^## Goal\s*\r?\n\r?\n- (.+?)(?:\r?\n|$)')
  if ($match.Success) {
    return $match.Groups[1].Value.Trim()
  }

  return ""
}

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $project = Read-ProjectConfig -Root $root -ProjectId $ProjectId
  $registry = Read-ProjectRegistry -Root $root -ProjectId $ProjectId
  $coordinationAiEntry = Get-CoordinationAiEntryFile -Root $root
  $coordinationPath = [string]$project["coordinationPath"]
  $coordinationStartHere = Join-Path (Get-CoordinationRoot -Root $root) "START_HERE.md"
  $activeContextFile = Get-CoordinationActiveContextMarkdownFile -Root $root
  $projectAiStartFile = Get-ProjectAiStartFile -Root $root -ProjectId $ProjectId

  $taskEntry = $null
  foreach ($entry in ($registry["tasks"] | ForEach-Object { $_ })) {
    if ($entry["taskId"] -eq $TaskId) {
      $taskEntry = $entry
      break
    }
  }

  $lockFile = Join-Path (Get-ProjectLocksPath -Root $root -ProjectId $ProjectId) ($TaskId + ".json")
  $lock = if (Test-Path -LiteralPath $lockFile) { Read-JsonFile -Path $lockFile } else { @{} }

  $resolvedSessionId = $SessionId
  if ([string]::IsNullOrWhiteSpace($resolvedSessionId) -and $null -ne $taskEntry -and $taskEntry.ContainsKey("sessionId")) {
    $resolvedSessionId = [string]$taskEntry["sessionId"]
  }
  if ([string]::IsNullOrWhiteSpace($resolvedSessionId) -and $lock.ContainsKey("sessionId")) {
    $resolvedSessionId = [string]$lock["sessionId"]
  }
  if ([string]::IsNullOrWhiteSpace($resolvedSessionId)) {
    throw "Could not resolve SessionId for task $TaskId."
  }

  $sessionEntry = $null
  foreach ($entry in ($registry["sessions"] | ForEach-Object { $_ })) {
    if ($entry["sessionId"] -eq $resolvedSessionId) {
      $sessionEntry = $entry
      break
    }
  }

  $resolvedAgentName = $AgentName
  if ([string]::IsNullOrWhiteSpace($resolvedAgentName) -and $null -ne $sessionEntry -and $sessionEntry.ContainsKey("owner")) {
    $resolvedAgentName = [string]$sessionEntry["owner"]
  }
  if ([string]::IsNullOrWhiteSpace($resolvedAgentName) -and $lock.ContainsKey("owner")) {
    $resolvedAgentName = [string]$lock["owner"]
  }

  $resolvedBranch = if ($null -ne $sessionEntry -and $sessionEntry.ContainsKey("branch")) { [string]$sessionEntry["branch"] } elseif ($lock.ContainsKey("branch")) { [string]$lock["branch"] } else { "" }
  $resolvedWorktree = if ($null -ne $sessionEntry -and $sessionEntry.ContainsKey("worktreePath")) { [string]$sessionEntry["worktreePath"] } elseif ($lock.ContainsKey("worktreePath")) { [string]$lock["worktreePath"] } else { "" }
  $taskFile = if ($null -ne $taskEntry -and $taskEntry.ContainsKey("taskFile")) { [string]$taskEntry["taskFile"] } else { Join-Path (Get-ProjectTasksPath -Root $root -ProjectId $ProjectId) ($TaskId + ".md") }
  $sessionDeltaFile = if ($null -ne $sessionEntry -and $sessionEntry.ContainsKey("sessionDeltaFile")) { [string]$sessionEntry["sessionDeltaFile"] } else { Get-ProjectSessionDeltaFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "active" }
  $scopeText = if ($lock.ContainsKey("scope") -and $null -ne $lock["scope"] -and @($lock["scope"] | ForEach-Object { $_ }).Count -gt 0) { [string]::Join(", ", @($lock["scope"] | ForEach-Object { $_ })) } else { "not set" }

  if (-not (Test-Path -LiteralPath $taskFile)) {
    throw "Task file not found: $taskFile"
  }

  $taskContent = Get-Content -LiteralPath $taskFile -Raw
  $goalSummary = Get-TaskGoalSummary -TaskContent $taskContent
  $baselineVersion = if ($registry.ContainsKey("baselineVersion")) { [string]$registry["baselineVersion"] } else { [string]$project["baselineVersion"] }

  $promptPath = if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath
  }
  elseif (Test-Path -LiteralPath $sessionDeltaFile) {
    if ($sessionDeltaFile -like "*\SESSIONS\active\*") {
      Get-ProjectSessionPromptFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "active"
    }
    else {
      Get-ProjectSessionPromptFile -Root $root -ProjectId $ProjectId -SessionId $resolvedSessionId -State "archive"
    }
  }
  else {
    Join-Path (Get-ProjectStagingPath -Root $root -ProjectId $ProjectId) ("PROMPT-" + $TaskId + ".md")
  }

  $readmeIndex = Get-ProjectReadmeIndexFile -Root $root -ProjectId $ProjectId
  $architectureTree = Get-ProjectArchitectureTreeFile -Root $root -ProjectId $ProjectId
  $architectureIndex = Get-ProjectArchitectureIndexFile -Root $root -ProjectId $ProjectId
  $liveStateBoard = Get-ProjectLiveStateBoardFile -Root $root -ProjectId $ProjectId
  $digestLatest = Get-ProjectDigestLatestSummaryFile -Root $root -ProjectId $ProjectId
  $activeSessionsIndex = Get-ProjectActiveSessionsIndexFile -Root $root -ProjectId $ProjectId
  $streamNewestEntry = Get-ProjectStreamNewestEntryFile -Root $root -ProjectId $ProjectId
  $recentIndex = Get-ProjectRecentIndexFile -Root $root -ProjectId $ProjectId
  $baselineState = Get-ProjectBaselineStateFile -Root $root -ProjectId $ProjectId
  $boardFile = Get-ProjectBoardFile -Root $root -ProjectId $ProjectId
  $handoffFile = Get-ProjectHandoffFile -Root $root -ProjectId $ProjectId
  $decisionsFile = Get-ProjectDecisionsFile -Root $root -ProjectId $ProjectId

  $prompt = @"
# AI Task Prompt

You are assigned to an active multi-AI coding session.

## Assignment

- Project: ``$ProjectId``
- Task: ``$TaskId``
- Session: ``$resolvedSessionId``
- Agent: ``$resolvedAgentName``
- Branch: ``$resolvedBranch``
- Worktree: ``$resolvedWorktree``
- Scope: ``$scopeText``
- Goal: ``$goalSummary``
- Baseline version: ``$baselineVersion``

## Cold Start Read Chain

Read these files in order before you inspect business code:

1. ``$coordinationAiEntry``
2. ``$activeContextFile``
3. ``$projectAiStartFile``
4. Follow ``$projectAiStartFile`` exactly. Its minimal chain starts with:
   - ``$liveStateBoard``
   - ``$digestLatest``
   - ``$activeSessionsIndex``
   - ``$streamNewestEntry``
5. Read ``$taskFile``
6. Read ``$lockFile``
7. Read ``$sessionDeltaFile``
8. Read this generated prompt: ``$promptPath``
9. Read ``$boardFile`` and ``$handoffFile`` if queue state or baton history matters
10. Read ``$decisionsFile`` if design decisions matter
11. Read ``$readmeIndex`` only if you need deeper topology or boundaries
12. Read ``$architectureTree`` and ``$architectureIndex`` only if deeper system structure matters
13. Read ``$recentIndex`` only if the task overlaps recent work
14. Read ``$baselineState`` only if baseline ``$baselineVersion`` is new to you

## Hard Rules

- Do not scan the repository before finishing the read chain above.
- Work only in ``$resolvedWorktree`` on branch ``$resolvedBranch``.
- Before an intent, plan shift, or decision boundary, run:
  ``coordination\bin\coord-log-thought.cmd -ProjectId $ProjectId -SessionId $resolvedSessionId -AgentId $resolvedAgentName -Content "<intent or decision>"``
- Before any meaningful read, write, modify, delete, patch, or tool action, run:
  ``coordination\bin\coord-log-operation.cmd -ProjectId $ProjectId -SessionId $resolvedSessionId -AgentId $resolvedAgentName -ActionType <type> -TargetPath <path> -DeltaSummary "<why>"``
- After concrete code or file changes, run:
  ``coordination\bin\coord-log-mod.cmd -ProjectId $ProjectId -SessionId $resolvedSessionId -AgentId $resolvedAgentName -Content "<what changed>" -RelatedFiles <path>``
- During multi-AI work, poll the shared tail with:
  ``coordination\bin\coord-read-stream.cmd -ProjectId $ProjectId -AgentId $resolvedAgentName``
- Do not edit another active lock scope directly.
- If another active lock covers your target path, submit a patch under ``STAGING\patches\``.
- Keep ``$taskFile``, ``$lockFile``, ``$sessionDeltaFile``, ``$boardFile``, and ``$handoffFile`` current as work changes.
- Before the operator closes the task, make sure the worktree is committed or otherwise clean enough to remove safely.
- When the task is complete, the operator should close it with:
  ``coordination\bin\coord-close-task.cmd -ProjectId $ProjectId -TaskId $TaskId -AgentName $resolvedAgentName -SessionId $resolvedSessionId``

## Session Files

- Task file: ``$taskFile``
- Lock file: ``$lockFile``
- Session delta: ``$sessionDeltaFile``
- Live state board: ``$liveStateBoard``
- Digest head: ``$digestLatest``
- Stream newest entry: ``$streamNewestEntry``
- Generated prompt: ``$promptPath``
"@

  if ($DryRun) {
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      projectId = $ProjectId
      taskId = $TaskId
      sessionId = $resolvedSessionId
      promptFile = $promptPath
      aiEntryFile = $coordinationAiEntry
      activeContextFile = $activeContextFile
      aiStartFile = $projectAiStartFile
      liveStateBoardFile = $liveStateBoard
      digestLatestFile = $digestLatest
      streamNewestEntryFile = $streamNewestEntry
      branch = $resolvedBranch
      worktreePath = $resolvedWorktree
    }
    exit 0
  }

  Write-TextFile -Path $promptPath -Content $prompt

  if (Test-Path -LiteralPath $sessionDeltaFile) {
    $sessionDelta = Read-JsonFile -Path $sessionDeltaFile
    if (-not $sessionDelta.ContainsKey("paths") -or -not ($sessionDelta["paths"] -is [hashtable])) {
      $sessionDelta["paths"] = @{}
    }
    $sessionDelta["paths"]["promptFile"] = $promptPath
    $sessionDelta["updatedAt"] = Get-NowIso
    Write-JsonFile -Path $sessionDeltaFile -Object $sessionDelta
  }

  if ($null -ne $sessionEntry) {
    $registry["sessions"] = Upsert-ListEntry -Items $registry["sessions"] -Item @{
      sessionId = $resolvedSessionId
      taskId = $TaskId
      owner = $resolvedAgentName
      status = if ($sessionEntry.ContainsKey("status")) { [string]$sessionEntry["status"] } else { "active" }
      branch = $resolvedBranch
      worktreePath = $resolvedWorktree
      sessionDeltaFile = $sessionDeltaFile
      promptFile = $promptPath
      updatedAt = Get-NowIso
    } -Key "sessionId"
    Write-ProjectRegistry -Root $root -ProjectId $ProjectId -Registry $registry
  }

  Update-CoordinationRuntimeIndexes -Root $root

  Write-JsonResult @{
    ok = $true
    dryRun = $false
    projectId = $ProjectId
    taskId = $TaskId
    sessionId = $resolvedSessionId
    promptFile = $promptPath
    aiEntryFile = $coordinationAiEntry
    activeContextFile = $activeContextFile
    aiStartFile = $projectAiStartFile
    liveStateBoardFile = $liveStateBoard
    digestLatestFile = $digestLatest
    streamNewestEntryFile = $streamNewestEntry
    branch = $resolvedBranch
    worktreePath = $resolvedWorktree
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
