[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectId,
  [Parameter(Mandatory = $true)]
  [string]$AgentId,
  [string]$PointerKey,
  [int]$MaxEntries = 20,
  [switch]$NoAdvance,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $pointer = Read-ProjectReadPointer -Root $root -ProjectId $ProjectId -AgentId $AgentId -PointerKey $PointerKey
  $tailFile = Get-ProjectStreamTailFile -Root $root -ProjectId $ProjectId
  $newestFile = Get-ProjectStreamNewestEntryFile -Root $root -ProjectId $ProjectId
  $liveStateBoardFile = Get-ProjectLiveStateBoardFile -Root $root -ProjectId $ProjectId
  $digestLatestFile = Get-ProjectDigestLatestSummaryFile -Root $root -ProjectId $ProjectId

  $tailState = if (Test-Path -LiteralPath $tailFile) { Read-JsonFile -Path $tailFile } else { @{ entries = @() } }
  $entries = if ($tailState.ContainsKey("entries")) { ConvertTo-ObjectArray -Value $tailState["entries"] } else { @() }
  $entries = @($entries | Sort-Object {
      if ($_.ContainsKey("timestamp")) { [DateTimeOffset]::Parse([string]($_["timestamp"])) } else { [DateTimeOffset]::MinValue }
    })
  $newest = if (Test-Path -LiteralPath $newestFile) { Read-JsonFile -Path $newestFile } else { @{} }

  $lastEntryId = if ($pointer.ContainsKey("lastEntryId")) { [string]$pointer["lastEntryId"] } else { "" }
  $unread = @()
  $gapDetected = $false

  if ([string]::IsNullOrWhiteSpace($lastEntryId)) {
    $unread = @($entries | Select-Object -Last $MaxEntries)
  }
  else {
    $matchIndex = -1
    for ($i = 0; $i -lt $entries.Count; $i += 1) {
      if (($entries[$i]).ContainsKey("entry_id") -and ([string]$entries[$i]["entry_id"] -eq $lastEntryId)) {
        $matchIndex = $i
        break
      }
    }

    if ($matchIndex -ge 0) {
      $unread = @($entries | Select-Object -Skip ($matchIndex + 1) -Last $MaxEntries)
    }
    elseif ($newest.ContainsKey("entry_id") -and ([string]$newest["entry_id"] -ne $lastEntryId)) {
      $gapDetected = $true
      $unread = @($entries | Select-Object -Last $MaxEntries)
    }
  }

  if ((-not $NoAdvance) -and (@($unread).Count -gt 0) -and (-not $DryRun)) {
    $lastUnread = $unread[-1]
    $pointer["lastEntryId"] = [string]$lastUnread["entry_id"]
    $pointer["lastTimestamp"] = [string]$lastUnread["timestamp"]
    Write-ProjectReadPointer -Root $root -ProjectId $ProjectId -AgentId $AgentId -PointerKey $PointerKey -Pointer $pointer
  }

  Write-JsonResult @{
    ok = $true
    dryRun = [bool]$DryRun
    projectId = $ProjectId
    agentId = $AgentId
    pointerFile = (Get-ProjectReadPointerFile -Root $root -ProjectId $ProjectId -AgentId $AgentId -PointerKey $PointerKey)
    gapDetected = $gapDetected
    newestEntry = if ($newest.Count -gt 0) { $newest } else { $null }
    unreadEntries = $unread
    liveStateBoardFile = $liveStateBoardFile
    digestLatestFile = $digestLatestFile
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
