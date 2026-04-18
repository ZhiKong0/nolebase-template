[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectId,
  [Parameter(Mandatory = $true)]
  [string]$SessionId,
  [string]$TaskId,
  [string]$AgentId,
  [string]$Summary,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

function Read-TriadLogRecords {
  param(
    [string]$Path,
    [int]$MaxItems = 5
  )

  if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path)) {
    return @()
  }

  $records = @()
  foreach ($line in (Get-Content -LiteralPath $Path | Select-Object -Last ($MaxItems * 3))) {
    if ([string]::IsNullOrWhiteSpace($line)) {
      continue
    }
    $parsed = ConvertFrom-JsonSafe -Text $line
    if ($null -ne $parsed) {
      $records += ,$parsed
    }
  }

  return @($records | Select-Object -Last $MaxItems)
}

function Limit-Words {
  param(
    [string]$Text,
    [int]$WordLimit
  )

  if ([string]::IsNullOrWhiteSpace($Text)) {
    return ""
  }

  $words = $Text -split '\s+'
  if (@($words).Count -le $WordLimit) {
    return $Text.Trim()
  }

  return ((@($words)[0..($WordLimit - 1)]) -join " ").Trim() + " ..."
}

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $location = Resolve-ProjectSessionLocation -Root $root -ProjectId $ProjectId -SessionId $SessionId
  if ($null -eq $location) {
    throw "Session location not found for digest generation: $SessionId"
  }

  $project = Read-ProjectConfig -Root $root -ProjectId $ProjectId
  $registry = Read-ProjectRegistry -Root $root -ProjectId $ProjectId
  $sessionDelta = Read-JsonFile -Path ([string]$location["sessionDeltaFile"])
  if ([string]::IsNullOrWhiteSpace($TaskId) -and $sessionDelta.ContainsKey("taskId")) {
    $TaskId = [string]$sessionDelta["taskId"]
  }
  if ([string]::IsNullOrWhiteSpace($AgentId) -and $sessionDelta.ContainsKey("agentId")) {
    $AgentId = [string]$sessionDelta["agentId"]
  }

  $thoughtLog = if ($sessionDelta.ContainsKey("paths") -and $sessionDelta["paths"].ContainsKey("thoughtLog")) { [string]$sessionDelta["paths"]["thoughtLog"] } else { Get-ProjectSessionTriadFile -Root $root -ProjectId $ProjectId -SessionId $SessionId -TriadType "THOUGHT" -State $location["state"] -Partition $location["partition"] }
  $execLog = if ($sessionDelta.ContainsKey("paths") -and $sessionDelta["paths"].ContainsKey("execLog")) { [string]$sessionDelta["paths"]["execLog"] } else { Get-ProjectSessionTriadFile -Root $root -ProjectId $ProjectId -SessionId $SessionId -TriadType "EXEC" -State $location["state"] -Partition $location["partition"] }
  $modLog = if ($sessionDelta.ContainsKey("paths") -and $sessionDelta["paths"].ContainsKey("modLog")) { [string]$sessionDelta["paths"]["modLog"] } else { Get-ProjectSessionTriadFile -Root $root -ProjectId $ProjectId -SessionId $SessionId -TriadType "MOD" -State $location["state"] -Partition $location["partition"] }

  $thoughts = Read-TriadLogRecords -Path $thoughtLog -MaxItems 4
  $execs = Read-TriadLogRecords -Path $execLog -MaxItems 4
  $mods = Read-TriadLogRecords -Path $modLog -MaxItems 4
  $files = if ($sessionDelta.ContainsKey("delta") -and $sessionDelta["delta"].ContainsKey("touchedFiles")) { @($sessionDelta["delta"]["touchedFiles"] | ForEach-Object { $_ }) } else { @() }
  $conflicts = if ($sessionDelta.ContainsKey("delta") -and $sessionDelta["delta"].ContainsKey("conflicts")) { @($sessionDelta["delta"]["conflicts"] | ForEach-Object { $_ }) } else { @() }
  $summaryText = if (-not [string]::IsNullOrWhiteSpace($Summary)) { $Summary } elseif ($sessionDelta.ContainsKey("delta") -and $sessionDelta["delta"].ContainsKey("summary")) { [string]$sessionDelta["delta"]["summary"] } else { "No explicit summary was recorded for this session." }
  $summaryText = Limit-Words -Text $summaryText -WordLimit (Get-DigestWordLimit)

  $decisionLines = @()
  foreach ($entry in $thoughts) {
    $decisionLines += ('- {0}' -f (Get-TextPreview -Text ([string]$entry["content"]) -MaxLength 180))
  }
  if (@($decisionLines).Count -eq 0) {
    $decisionLines = @("- No THOUGHT records were captured.")
  }

  $execLines = @()
  foreach ($entry in $execs) {
    $execLines += ('- {0}' -f (Get-TextPreview -Text ([string]$entry["content"]) -MaxLength 180))
  }
  if (@($execLines).Count -eq 0) {
    $execLines = @("- No EXEC records were captured.")
  }

  $modLines = @()
  foreach ($entry in $mods) {
    $modLines += ('- {0}' -f (Get-TextPreview -Text ([string]$entry["content"]) -MaxLength 180))
  }
  if (@($modLines).Count -eq 0) {
    $modLines = @("- No MOD records were captured.")
  }

  $conflictLines = @()
  foreach ($entry in ($conflicts | Select-Object -Last 4)) {
    if ($entry -is [hashtable] -and $entry.ContainsKey("targetPath")) {
      $conflictLines += ('- target=`{0}` conflict_task=`{1}`' -f [string]$entry["targetPath"], [string]$entry["conflictingTaskId"])
    }
  }
  if (@($conflictLines).Count -eq 0) {
    $conflictLines = @("- No conflict escalations were recorded.")
  }

  $digestFile = Get-ProjectDigestFile -Root $root -ProjectId $ProjectId -SessionId $SessionId
  $digestId = "DIGEST-$SessionId"
  $now = Get-NowIso

  $digestContent = @(
    "---"
    "file_type: digest_node"
    "project_id: $ProjectId"
    "session_id: $SessionId"
    "task_id: $TaskId"
    "agent_id: $AgentId"
    "digest_id: $digestId"
    "write_mode: script_managed"
    "last_updated: $now"
    "---"
    "# Digest $SessionId"
    ""
    "## Summary"
    ""
    $summaryText
    ""
    "## Key Decisions"
    ""
  ) + $decisionLines + @(
    ""
    "## Execution Highlights"
    ""
  ) + $execLines + @(
    ""
    "## Modifications"
    ""
  ) + $modLines + @(
    ""
    "## Touched Files"
    ""
  )

  if (@($files).Count -eq 0) {
    $digestContent += "- No touched files were recorded."
  }
  else {
    foreach ($file in ($files | Select-Object -First 12)) {
      $digestContent += ('- `{0}`' -f [string]$file)
    }
  }

  $digestContent += @(
    ""
    "## Lessons and Blockers"
    ""
  ) + $conflictLines + @(
    ""
    "## Next Read"
    ""
    '- `context/LIVE_STATE_BOARD.md`'
    '- `CHANGELOG_TIER/L3_DIGEST/latest_summary.md`'
    '- `CHANGELOG_TIER/L1_ACTIVE/ACTIVE_SESSIONS.json`'
  )

  if ($DryRun) {
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      projectId = $ProjectId
      sessionId = $SessionId
      digestFile = $digestFile
      digestId = $digestId
    }
    exit 0
  }

  Write-TextFile -Path $digestFile -Content ([string]::Join([Environment]::NewLine, $digestContent))

  $digestIndexFile = Get-ProjectDigestIndexFile -Root $root -ProjectId $ProjectId
  $digestIndex = if (Test-Path -LiteralPath $digestIndexFile) { Read-JsonFile -Path $digestIndexFile } else { @{ version = 1; updatedAt = $now; latestDigestId = $null; digests = @() } }
  if (-not $digestIndex.ContainsKey("digests")) {
    $digestIndex["digests"] = @()
  }
  $digestEntry = @{
    digestId = $digestId
    sessionId = $SessionId
    taskId = $TaskId
    agentId = $AgentId
    file = $digestFile
    summary = $summaryText
    relatedFiles = $files
    createdAt = $now
    updatedAt = $now
  }
  $digestIndex["digests"] = Upsert-ListEntry -Items $digestIndex["digests"] -Item $digestEntry -Key "digestId"
  $digestIndex["latestDigestId"] = $digestId
  $digestIndex["updatedAt"] = $now
  Write-JsonFile -Path $digestIndexFile -Object $digestIndex

  $latestSummaryFile = Get-ProjectDigestLatestSummaryFile -Root $root -ProjectId $ProjectId
  $latestDigestEntries = @((ConvertTo-ObjectArray -Value $digestIndex["digests"]) | Sort-Object {
      if ($_.ContainsKey("createdAt")) { [DateTimeOffset]::Parse([string]($_["createdAt"])) } else { [DateTimeOffset]::MinValue }
    } -Descending | Select-Object -First (Get-DigestSessionRetentionCount))
  $latestSummaryContent = @(
    "---"
    "file_type: digest_head"
    "project_id: $ProjectId"
    "write_mode: script_managed"
    "last_updated: $now"
    "---"
    "# Latest Summary"
    ""
    "## Current Whole-Picture Shortcut"
    ""
    $summaryText
    ""
    "## Digest Chain"
    ""
  )
  foreach ($entry in $latestDigestEntries) {
    $latestSummaryContent += ('- `{0}` session=`{1}` task=`{2}` summary={3}' -f [string]$entry["digestId"], [string]$entry["sessionId"], [string]$entry["taskId"], (Get-TextPreview -Text ([string]$entry["summary"]) -MaxLength 140))
  }
  Write-TextFile -Path $latestSummaryFile -Content ([string]::Join([Environment]::NewLine, $latestSummaryContent))

  if (-not $sessionDelta.ContainsKey("digestState") -or -not ($sessionDelta["digestState"] -is [hashtable])) {
    $sessionDelta["digestState"] = @{}
  }
  if (-not $sessionDelta.ContainsKey("paths") -or -not ($sessionDelta["paths"] -is [hashtable])) {
    $sessionDelta["paths"] = @{}
  }
  $sessionDelta["digestState"]["latestDigestFile"] = $digestFile
  $sessionDelta["digestState"]["latestDigestId"] = $digestId
  $sessionDelta["paths"]["digestLatestSummary"] = $latestSummaryFile
  $sessionDelta["updatedAt"] = $now
  Write-JsonFile -Path ([string]$location["sessionDeltaFile"]) -Object $sessionDelta

  $registry["tiers"]["l3Digest"] = Upsert-ListEntry -Items $registry["tiers"]["l3Digest"] -Item @{
    digestId = $digestId
    sessionId = $SessionId
    taskId = $TaskId
    file = $digestFile
    updatedAt = $now
  } -Key "digestId"
  Write-ProjectRegistry -Root $root -ProjectId $ProjectId -Registry $registry
  Update-LiveStateBoard -Root $root -ProjectId $ProjectId
  Update-CoordinationRuntimeIndexes -Root $root

  Write-JsonResult @{
    ok = $true
    dryRun = $false
    projectId = $ProjectId
    sessionId = $SessionId
    digestId = $digestId
    digestFile = $digestFile
    latestSummaryFile = $latestSummaryFile
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
