[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectId,
  [Parameter(Mandatory = $true)]
  [string]$TaskId,
  [string]$AgentName,
  [string]$SessionId,
  [string]$Summary,
  [switch]$DeleteBranchIfMerged,
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

function Update-TaskClosedContent {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [string]$Summary,
    [Parameter(Mandatory = $true)]
    [string]$NowIso
  )

  $content = Get-Content -LiteralPath $Path -Raw
  $content = [regex]::Replace($content, '(?m)^status:\s*.*$', "status: Closed")
  $content = [regex]::Replace($content, '(?m)^last_updated:\s*.*$', "last_updated: $NowIso")
  $content = [regex]::Replace($content, '(?m)^- Status: `[^`]+`$', "- Status: ``Closed``")
  $content = [regex]::Replace($content, '(?m)^- Updated at: .+$', "- Updated at: " + (Get-Date -Format "yyyy-MM-dd"))
  if (-not [string]::IsNullOrWhiteSpace($Summary)) {
    $content = [regex]::Replace($content, '(?m)^- Next action:.*$', "- Next action: " + $Summary)
    if ($content -notmatch [regex]::Escape($Summary)) {
      $content += [Environment]::NewLine + "- Closed note: $Summary"
    }
  }
  else {
    $content = [regex]::Replace($content, '(?m)^- Next action:.*$', "- Next action: Start the next queued task.")
  }
  $content = [regex]::Replace($content, '(?m)^- Next person:.*$', "- Next person: next AI or operator")
  $content = [regex]::Replace($content, '(?m)^- What changed in shared context:.*$', "- What changed in shared context: The task was closed and the active session was archived.")
  Write-TextFile -Path $Path -Content $content
}

function Update-BoardDone {
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
    [Parameter(Mandatory = $true)]
    [string]$AgentName,
    [Parameter(Mandatory = $true)]
    [string]$TaskId,
    [Parameter(Mandatory = $true)]
    [string]$SessionId,
    [string]$BranchName,
    [string]$WorktreePath,
    [string]$Summary
  )

  $content = Get-Content -LiteralPath $Path -Raw
  $latestBlock = @"
## Latest

- Time: $(Get-Date -Format "yyyy-MM-dd")
- From: $AgentName
- To: next AI or operator
- Session: $SessionId
- Task: $TaskId
- Status: closed
- Branch: $(if ([string]::IsNullOrWhiteSpace($BranchName)) { "unknown" } else { $BranchName })
- Worktree: $(if ([string]::IsNullOrWhiteSpace($WorktreePath)) { "unknown" } else { $WorktreePath })
- Files touched:
- Next step: $(if ([string]::IsNullOrWhiteSpace($Summary)) { "Start the next queued task." } else { $Summary })
- Risk or blocker:
"@
  $content = [regex]::Replace($content, '(?s)## Latest\s*.*?\s*## Rule', $latestBlock + [Environment]::NewLine + [Environment]::NewLine + '## Rule')
  Write-TextFile -Path $Path -Content $content
}

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
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
  $resolvedSessionId = if (-not [string]::IsNullOrWhiteSpace($SessionId)) { $SessionId } elseif ($lock.ContainsKey("sessionId")) { [string]$lock["sessionId"] } elseif ($null -ne $taskEntry -and $taskEntry.ContainsKey("sessionId")) { [string]$taskEntry["sessionId"] } else { "" }
  $resolvedAgentName = if (-not [string]::IsNullOrWhiteSpace($AgentName)) { $AgentName } elseif ($lock.ContainsKey("owner")) { [string]$lock["owner"] } elseif ($null -ne $taskEntry -and $taskEntry.ContainsKey("owner")) { [string]$taskEntry["owner"] } else { "unknown" }

  $releaseArgs = @("-ProjectId", $ProjectId, "-TaskId", $TaskId)
  if (-not [string]::IsNullOrWhiteSpace($resolvedAgentName)) { $releaseArgs += @("-AgentName", $resolvedAgentName) }
  if (-not [string]::IsNullOrWhiteSpace($resolvedSessionId)) { $releaseArgs += @("-SessionId", $resolvedSessionId) }
  if ($DeleteBranchIfMerged) { $releaseArgs += "-DeleteBranchIfMerged" }
  if ($DryRun) { $releaseArgs += "-DryRun" }

  $releaseResult = Invoke-JsonScript -ScriptPath (Join-Path $PSScriptRoot "coord_release_worktree.ps1") -Arguments $releaseArgs
  if ($releaseResult.exitCode -ne 0) {
    throw $releaseResult.json.error
  }

  if ($DryRun) {
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      projectId = $ProjectId
      taskId = $TaskId
      sessionId = $resolvedSessionId
      nextState = "closed"
    }
    exit 0
  }

  $taskFile = if ($null -ne $taskEntry -and $taskEntry.ContainsKey("taskFile")) { [string]$taskEntry["taskFile"] } else { Join-Path (Get-ProjectTasksPath -Root $root -ProjectId $ProjectId) ($TaskId + ".md") }
  $now = Get-NowIso

  if (Test-Path -LiteralPath $taskFile) {
    Update-TaskClosedContent -Path $taskFile -Summary $Summary -NowIso $now
  }

  $resolvedBranchName = if ($releaseResult.json.branchName) { [string]$releaseResult.json.branchName } elseif ($null -ne $taskEntry -and $taskEntry.ContainsKey("branch")) { [string]$taskEntry["branch"] } elseif ($lock.ContainsKey("branch")) { [string]$lock["branch"] } else { "" }
  $resolvedWorktreePath = if ($releaseResult.json.worktreePath) { [string]$releaseResult.json.worktreePath } elseif ($null -ne $taskEntry -and $taskEntry.ContainsKey("worktreePath")) { [string]$taskEntry["worktreePath"] } elseif ($lock.ContainsKey("worktreePath")) { [string]$lock["worktreePath"] } else { "" }

  $digestArgs = @(
    "-ProjectId", $ProjectId,
    "-SessionId", $resolvedSessionId,
    "-TaskId", $TaskId,
    "-AgentId", $resolvedAgentName
  )
  if (-not [string]::IsNullOrWhiteSpace($Summary)) {
    $digestArgs += @("-Summary", $Summary)
  }
  $digestResult = Invoke-JsonScript -ScriptPath (Join-Path $PSScriptRoot "coord_context_digest.ps1") -Arguments $digestArgs
  if ($digestResult.exitCode -ne 0) {
    throw $digestResult.json.error
  }

  Update-BoardDone -Path (Get-ProjectBoardFile -Root $root -ProjectId $ProjectId) -TaskId $TaskId -Entry ("- `[Done]` {0} closed by {1} session={2}" -f $TaskId, $resolvedAgentName, $resolvedSessionId)
  Update-HandoffSnapshot -Path (Get-ProjectHandoffFile -Root $root -ProjectId $ProjectId) -AgentName $resolvedAgentName -TaskId $TaskId -SessionId $resolvedSessionId -BranchName $resolvedBranchName -WorktreePath $resolvedWorktreePath -Summary $Summary

  $registry = Read-ProjectRegistry -Root $root -ProjectId $ProjectId
  $registry["tasks"] = Upsert-ListEntry -Items $registry["tasks"] -Item @{
    taskId = $TaskId
    sessionId = $resolvedSessionId
    owner = $resolvedAgentName
    taskFile = $taskFile
    branch = $resolvedBranchName
    worktreePath = $resolvedWorktreePath
    status = "closed"
    closeSummary = $Summary
    closedAt = $now
    updatedAt = $now
  } -Key "taskId"
  Write-ProjectRegistry -Root $root -ProjectId $ProjectId -Registry $registry
  Update-CoordinationRuntimeIndexes -Root $root

  Write-JsonResult @{
    ok = $true
    dryRun = $false
    projectId = $ProjectId
    taskId = $TaskId
    sessionId = $resolvedSessionId
    status = "closed"
    taskFile = $taskFile
    digestFile = if ($digestResult.json.digestFile) { [string]$digestResult.json.digestFile } else { $null }
    digestLatestSummaryFile = if ($digestResult.json.latestSummaryFile) { [string]$digestResult.json.latestSummaryFile } else { $null }
    activeContextFile = (Get-CoordinationActiveContextMarkdownFile -Root $root)
    aiStartFile = (Get-ProjectAiStartFile -Root $root -ProjectId $ProjectId)
    nextStep = "The task is closed and its digest was appended. Use coord-generate-prompt only if a follow-up session needs to be spawned."
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
