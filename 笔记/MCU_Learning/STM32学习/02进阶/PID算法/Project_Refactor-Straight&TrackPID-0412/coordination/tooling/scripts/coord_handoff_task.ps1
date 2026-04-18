[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectId,
  [Parameter(Mandatory = $true)]
  [string]$TaskId,
  [Parameter(Mandatory = $true)]
  [string]$ToAgentName,
  [string]$FromAgentName,
  [string]$SessionId,
  [string]$NewSessionId,
  [string]$Goal,
  [string]$Summary,
  [string]$BaseBranch,
  [string]$BranchName,
  [string]$WorktreeName,
  [string[]]$Scope = @(),
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

function Update-HandoffSnapshot {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    [string]$FromAgent,
    [Parameter(Mandatory = $true)]
    [string]$ToAgent,
    [Parameter(Mandatory = $true)]
    [string]$TaskId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [Parameter(Mandatory = $true)]
    [string]$BranchName,
    [Parameter(Mandatory = $true)]
    [string]$WorktreePath,
    [string]$Summary
  )

  $content = Get-Content -LiteralPath $Path -Raw
  $latestBlock = @"
## Latest

- Time: $(Get-Date -Format "yyyy-MM-dd")
- From: $FromAgent
- To: $ToAgent
- Session: $SessionId
- Task: $TaskId
- Status: handed_off
- Branch: $BranchName
- Worktree: $WorktreePath
- Files touched:
- Next step: $(if ([string]::IsNullOrWhiteSpace($Summary)) { "Read the generated AI prompt and continue the task." } else { $Summary })
- Risk or blocker:
"@
  $content = [regex]::Replace($content, '(?s)## Latest\s*.*?\s*## Rule', $latestBlock + [Environment]::NewLine + [Environment]::NewLine + '## Rule')
  Write-TextFile -Path $Path -Content $content
}

function Update-TaskHandoffSection {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    [string]$ToAgent,
    [string]$Summary
  )

  $content = Get-Content -LiteralPath $Path -Raw
  $nextAction = if ([string]::IsNullOrWhiteSpace($Summary)) { "Read the generated AI prompt and continue." } else { $Summary }
  $content = [regex]::Replace($content, '(?m)^- Next person:.*$', "- Next person: $ToAgent")
  $content = [regex]::Replace($content, '(?m)^- Next action:.*$', "- Next action: " + $nextAction)
  $content = [regex]::Replace($content, '(?m)^- What changed in shared context:.*$', "- What changed in shared context: Task ownership was handed off to $ToAgent.")
  Write-TextFile -Path $Path -Content $content
}

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $inputScope = ConvertTo-StringArray -Value $Scope
  $registry = Read-ProjectRegistry -Root $root -ProjectId $ProjectId
  $taskEntry = $null
  foreach ($entry in ($registry["tasks"] | ForEach-Object { $_ })) {
    if ($entry["taskId"] -eq $TaskId) {
      $taskEntry = $entry
      break
    }
  }

  $lockFile = Join-Path (Get-ProjectLocksPath -Root $root -ProjectId $ProjectId) ($TaskId + ".json")
  $lock = if (Test-Path -LiteralPath $lockFile) { Read-JsonFile -Path $lockFile } else { @{} }
  $resolvedFromAgent = if (-not [string]::IsNullOrWhiteSpace($FromAgentName)) { $FromAgentName } elseif ($lock.ContainsKey("owner")) { [string]$lock["owner"] } elseif ($null -ne $taskEntry -and $taskEntry.ContainsKey("owner")) { [string]$taskEntry["owner"] } else { "" }
  $resolvedSessionId = if (-not [string]::IsNullOrWhiteSpace($SessionId)) { $SessionId } elseif ($lock.ContainsKey("sessionId")) { [string]$lock["sessionId"] } elseif ($null -ne $taskEntry -and $taskEntry.ContainsKey("sessionId")) { [string]$taskEntry["sessionId"] } else { "" }
  $resolvedBranchName = if (-not [string]::IsNullOrWhiteSpace($BranchName)) { $BranchName } elseif ($lock.ContainsKey("branch")) { [string]$lock["branch"] } elseif ($null -ne $taskEntry -and $taskEntry.ContainsKey("branch")) { [string]$taskEntry["branch"] } else { "" }
  $resolvedBaseBranch = if (-not [string]::IsNullOrWhiteSpace($BaseBranch)) { $BaseBranch } elseif ($null -ne $taskEntry -and $taskEntry.ContainsKey("baseBranch")) { [string]$taskEntry["baseBranch"] } else { "" }
  $scopeSource = if ($inputScope.Count -gt 0) { $inputScope } elseif ($lock.ContainsKey("scope") -and $null -ne $lock["scope"]) { $lock["scope"] } else { @() }
  $resolvedScope = ConvertTo-StringArray -Value $scopeSource

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
  if (-not $taskIsActive) {
    throw "Task $TaskId is not active. Use coord-reopen-task to resume a closed or released task."
  }

  if ([string]::IsNullOrWhiteSpace($resolvedFromAgent)) {
    throw "Could not resolve the current owner for task $TaskId."
  }
  if ([string]::IsNullOrWhiteSpace($resolvedSessionId)) {
    throw "Could not resolve the current session for task $TaskId."
  }

  $releaseArgs = @(
    "-ProjectId", $ProjectId,
    "-TaskId", $TaskId,
    "-AgentName", $resolvedFromAgent,
    "-SessionId", $resolvedSessionId
  )
  if ($DryRun) { $releaseArgs += "-DryRun" }

  $reopenArgs = @(
    "-ProjectId", $ProjectId,
    "-TaskId", $TaskId,
    "-AgentName", $ToAgentName
  )
  if (-not [string]::IsNullOrWhiteSpace($NewSessionId)) { $reopenArgs += @("-SessionId", $NewSessionId) }
  if (-not [string]::IsNullOrWhiteSpace($Goal)) { $reopenArgs += @("-Goal", $Goal) }
  if (-not [string]::IsNullOrWhiteSpace($resolvedBaseBranch)) { $reopenArgs += @("-BaseBranch", $resolvedBaseBranch) }
  if (-not [string]::IsNullOrWhiteSpace($resolvedBranchName)) { $reopenArgs += @("-BranchName", $resolvedBranchName) }
  if (-not [string]::IsNullOrWhiteSpace($WorktreeName)) { $reopenArgs += @("-WorktreeName", $WorktreeName) }
  foreach ($entry in $resolvedScope) { $reopenArgs += @("-Scope", $entry) }
  $handoffSummary = if ([string]::IsNullOrWhiteSpace($Summary)) { "Handoff from $resolvedFromAgent to $ToAgentName." } else { $Summary }
  $reopenArgs += @("-ReopenReason", $handoffSummary)
  if ($DryRun) { $reopenArgs += "-DryRun" }

  $releaseResult = Invoke-JsonScript -ScriptPath (Join-Path $PSScriptRoot "coord_release_worktree.ps1") -Arguments $releaseArgs
  if ($releaseResult.exitCode -ne 0) {
    throw $releaseResult.json.error
  }

  $reopenResult = Invoke-JsonScript -ScriptPath (Join-Path $PSScriptRoot "coord_reopen_task.ps1") -Arguments $reopenArgs
  if ($reopenResult.exitCode -ne 0) {
    throw $reopenResult.json.error
  }

  if ($DryRun) {
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      projectId = $ProjectId
      taskId = $TaskId
      fromAgent = $resolvedFromAgent
      toAgent = $ToAgentName
      previousSessionId = $resolvedSessionId
      nextSessionId = $reopenResult.json.sessionId
      branchName = $reopenResult.json.branchName
      worktreePath = $reopenResult.json.worktreePath
      promptFile = $reopenResult.json.promptFile
      aiStartFile = $reopenResult.json.aiStartFile
      activeContextFile = $reopenResult.json.activeContextFile
    }
    exit 0
  }

  $taskFile = if ($null -ne $taskEntry -and $taskEntry.ContainsKey("taskFile")) { [string]$taskEntry["taskFile"] } else { Join-Path (Get-ProjectTasksPath -Root $root -ProjectId $ProjectId) ($TaskId + ".md") }
  if (Test-Path -LiteralPath $taskFile) {
    Update-TaskHandoffSection -Path $taskFile -ToAgent $ToAgentName -Summary $Summary
  }

  Update-HandoffSnapshot -Path (Get-ProjectHandoffFile -Root $root -ProjectId $ProjectId) -FromAgent $resolvedFromAgent -ToAgent $ToAgentName -TaskId $TaskId -SessionId ([string]$reopenResult.json.sessionId) -BranchName ([string]$reopenResult.json.branchName) -WorktreePath ([string]$reopenResult.json.worktreePath) -Summary $Summary

  Write-JsonResult @{
    ok = $true
    dryRun = $false
    projectId = $ProjectId
    taskId = $TaskId
    fromAgent = $resolvedFromAgent
    toAgent = $ToAgentName
    previousSessionId = $resolvedSessionId
    nextSessionId = $reopenResult.json.sessionId
    branchName = $reopenResult.json.branchName
    worktreePath = $reopenResult.json.worktreePath
    promptFile = $reopenResult.json.promptFile
    aiStartFile = $reopenResult.json.aiStartFile
    activeContextFile = $reopenResult.json.activeContextFile
    nextStep = "Send the generated prompt file to the next AI and continue THOUGHT/EXEC/MOD logging in the new session."
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
