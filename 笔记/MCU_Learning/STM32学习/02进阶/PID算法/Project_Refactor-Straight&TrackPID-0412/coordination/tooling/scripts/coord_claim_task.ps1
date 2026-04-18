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
  [switch]$Force,
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
    [string]$NowIso
  )

  $content = Get-Content -LiteralPath $Path -Raw
  $content = [regex]::Replace($content, '(?m)^session_id:\s*.*$', "session_id: $SessionId")
  $content = [regex]::Replace($content, '(?m)^owner:\s*.*$', "owner: $AgentName")
  $content = [regex]::Replace($content, '(?m)^status:\s*.*$', "status: InProgress")
  $content = [regex]::Replace($content, '(?m)^last_updated:\s*.*$', "last_updated: $NowIso")
  $content = [regex]::Replace($content, '(?m)^- Status: `[^`]+`$', "- Status: ``In Progress``")
  $content = [regex]::Replace($content, '(?m)^- Updated at: .+$', "- Updated at: " + (Get-Date -Format "yyyy-MM-dd"))
  if (-not [string]::IsNullOrWhiteSpace($Goal) -and $content.Contains("- Describe the concrete outcome.")) {
    $content = $content.Replace("- Describe the concrete outcome.", "- $Goal")
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

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $now = Get-NowIso
  $normalizedScope = ConvertTo-StringArray -Value $Scope
  $scopeText = if ($normalizedScope.Count -gt 0) { [string]::Join(", ", $normalizedScope) } else { "not set" }

  $newArgs = @(
    "-ProjectId", $ProjectId,
    "-AgentName", $AgentName,
    "-TaskId", $TaskId
  )
  if (-not [string]::IsNullOrWhiteSpace($SessionId)) { $newArgs += @("-SessionId", $SessionId) }
  if (-not [string]::IsNullOrWhiteSpace($BaseBranch)) { $newArgs += @("-BaseBranch", $BaseBranch) }
  if (-not [string]::IsNullOrWhiteSpace($BranchName)) { $newArgs += @("-BranchName", $BranchName) }
  if (-not [string]::IsNullOrWhiteSpace($WorktreeName)) { $newArgs += @("-WorktreeName", $WorktreeName) }
  foreach ($entry in $normalizedScope) { $newArgs += @("-Scope", $entry) }
  if ($Force) { $newArgs += "-Force" }
  if ($DryRun) { $newArgs += "-DryRun" }

  $newResult = Invoke-JsonScript -ScriptPath (Join-Path $PSScriptRoot "coord_new_worktree.ps1") -Arguments $newArgs
  if ($newResult.exitCode -ne 0) {
    throw $newResult.json.error
  }

  $sessionIdValue = [string]$newResult.json.sessionId
  $taskFile = [string]$newResult.json.taskFile
  $promptFile = Get-ProjectSessionPromptFile -Root $root -ProjectId $ProjectId -SessionId $sessionIdValue -State "active"

  if ($DryRun) {
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      projectId = $ProjectId
      taskId = $TaskId
      sessionId = $sessionIdValue
      branchName = $newResult.json.branchName
      worktreePath = $newResult.json.worktreePath
      promptFile = $promptFile
      aiStartFile = $newResult.json.aiStartFile
      activeContextFile = $newResult.json.activeContextFile
      scope = $normalizedScope
    }
    exit 0
  }

  Update-TaskMarkdown -Path $taskFile -Goal $Goal -SessionId $sessionIdValue -AgentName $AgentName -NowIso $now
  Update-BoardFile -Path (Get-ProjectBoardFile -Root $root -ProjectId $ProjectId) -TaskId $TaskId -Entry ("- `[In Progress]` {0} owner={1} session={2} scope={3}" -f $TaskId, $AgentName, $sessionIdValue, $scopeText)

  $promptArgs = @(
    "-ProjectId", $ProjectId,
    "-TaskId", $TaskId,
    "-SessionId", $sessionIdValue,
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
    agentName = $AgentName
    sessionId = $sessionIdValue
    branchName = $newResult.json.branchName
    worktreePath = $newResult.json.worktreePath
    taskFile = $taskFile
    lockFile = $newResult.json.lockFile
    sessionDeltaFile = $newResult.json.sessionDeltaFile
    promptFile = $promptResult.json.promptFile
    aiStartFile = $promptResult.json.aiStartFile
    activeContextFile = $promptResult.json.activeContextFile
    nextStep = "Send the generated prompt file to the assigned AI, then start THOUGHT/EXEC/MOD logging through coord-log-thought, coord-log-operation, and coord-log-mod."
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
