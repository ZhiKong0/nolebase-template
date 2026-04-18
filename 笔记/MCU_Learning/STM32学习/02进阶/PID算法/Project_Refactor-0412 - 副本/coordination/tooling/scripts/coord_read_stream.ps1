[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectId,
  [Parameter(Mandatory = $true)]
  [string]$AgentId,
  [string]$PointerKey,
  [string[]]$Types = @(),
  [int]$MaxEntries = 20,
  [switch]$OtherAgentsOnly,
  [switch]$IncludeSessionHeads,
  [switch]$AcceptLossyBoundary,
  [switch]$NoAdvance,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $resolvedPointerKey = if ([string]::IsNullOrWhiteSpace($PointerKey)) {
    Get-ReadModeSignature -Types $Types -OtherAgentsOnly:$OtherAgentsOnly
  }
  else {
    $PointerKey
  }

  $pointer = Read-ProjectReadPointer -Root $root -ProjectId $ProjectId -AgentId $AgentId -PointerKey $resolvedPointerKey
  $streamFile = Get-ProjectStreamEntriesFile -Root $root -ProjectId $ProjectId
  $newestFile = Get-ProjectStreamNewestEntryFile -Root $root -ProjectId $ProjectId
  $sessionHeadsFile = Get-ProjectSessionHeadsFile -Root $root -ProjectId $ProjectId
  $liveStateBoardFile = Get-ProjectLiveStateBoardFile -Root $root -ProjectId $ProjectId
  $digestLatestFile = Get-ProjectDigestLatestSummaryFile -Root $root -ProjectId $ProjectId
  $modeSignature = Get-ReadModeSignature -Types $Types -OtherAgentsOnly:$OtherAgentsOnly

  if ($pointer.ContainsKey("modeSignature") -and -not [string]::IsNullOrWhiteSpace([string]$pointer["modeSignature"]) -and ([string]$pointer["modeSignature"] -ne $modeSignature)) {
    throw ("Pointer mode mismatch for key {0}. Existing mode={1}, requested mode={2}. Use a different -PointerKey or delete the stale pointer file." -f $resolvedPointerKey, [string]$pointer["modeSignature"], $modeSignature)
  }

  $entries = Read-JsonLineObjects -Path $streamFile
  $entries = @($entries | Sort-Object {
      if ($_.ContainsKey("timestamp")) { [DateTimeOffset]::Parse([string]($_["timestamp"])) } else { [DateTimeOffset]::MinValue }
    })
  $newest = if (Test-Path -LiteralPath $newestFile) { Read-JsonFile -Path $newestFile } else { @{} }

  $lastEntryId = if ($pointer.ContainsKey("lastEntryId")) { [string]$pointer["lastEntryId"] } else { "" }
  $unread = @()
  $boundaryBlocked = $false
  $lossyBoundary = $null

  if ([string]::IsNullOrWhiteSpace($lastEntryId)) {
    $unread = @($entries)
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
      $unread = @($entries | Select-Object -Skip ($matchIndex + 1))
    }
    elseif ($newest.ContainsKey("entry_id") -and ([string]$newest["entry_id"] -ne $lastEntryId)) {
      $lossyBoundary = @{
        detected = $true
        accepted = [bool]$AcceptLossyBoundary
        reason = "pointer_not_found_in_stream"
        lastEntryId = $lastEntryId
        streamFile = $streamFile
        recommendedReads = @(
          $liveStateBoardFile,
          $digestLatestFile,
          (Get-ProjectActiveSessionsIndexFile -Root $root -ProjectId $ProjectId),
          $newestFile
        )
        note = "The pointer target no longer exists in the current stream source. Read the recommended summary chain first, then rerun with -AcceptLossyBoundary if you want to resume from the current stream head."
      }
      if ($AcceptLossyBoundary) {
        $unread = @($entries)
      }
      else {
        $boundaryBlocked = $true
        $unread = @()
      }
    }
  }

  $unreadBeforeFilters = @($unread)
  $normalizedTypes = @()
  foreach ($typeValue in (ConvertTo-StringArray -Value $Types)) {
    switch ($typeValue.Trim().ToUpperInvariant()) {
      "THOUGHT" { $normalizedTypes += "T" }
      "EXEC" { $normalizedTypes += "E" }
      "MOD" { $normalizedTypes += "M" }
      "T" { $normalizedTypes += "T" }
      "E" { $normalizedTypes += "E" }
      "M" { $normalizedTypes += "M" }
      default { }
    }
  }
  $normalizedTypes = @($normalizedTypes | Select-Object -Unique)

  if ($OtherAgentsOnly) {
    $unread = ConvertTo-ObjectArray -Value ($unread | Where-Object {
        $_.ContainsKey("agent_id") -and ([string]$_["agent_id"] -ne $AgentId)
      })
  }
  if (@($normalizedTypes).Count -gt 0) {
    $unread = ConvertTo-ObjectArray -Value ($unread | Where-Object {
        $_.ContainsKey("type") -and ($normalizedTypes -contains ([string]$_["type"]).ToUpperInvariant())
      })
  }

  $returnedEntries = if ($MaxEntries -gt 0) { ConvertTo-ObjectArray -Value ($unread | Select-Object -First $MaxEntries) } else { ConvertTo-ObjectArray -Value $unread }
  $remainingUnreadCount = [Math]::Max(0, @($unread).Count - @($returnedEntries).Count)

  if ((-not $NoAdvance) -and (-not $DryRun) -and (-not $boundaryBlocked) -and (@($returnedEntries).Count -gt 0)) {
    $lastUnread = $returnedEntries[-1]
    $pointer["lastEntryId"] = [string]$lastUnread["entry_id"]
    $pointer["lastTimestamp"] = [string]$lastUnread["timestamp"]
    $pointer["modeSignature"] = $modeSignature
    $pointer["sourceStreamFile"] = $streamFile
    $pointer["lastBatchReturned"] = @($returnedEntries).Count
    $pointer["remainingUnreadCount"] = $remainingUnreadCount
    if ($null -ne $lossyBoundary) {
      $pointer["lossyBoundaryAcceptedAt"] = if ($AcceptLossyBoundary) { Get-NowIso } else { $null }
      $pointer["lossyBoundaryReason"] = [string]$lossyBoundary["reason"]
    }
    Write-ProjectReadPointer -Root $root -ProjectId $ProjectId -AgentId $AgentId -PointerKey $resolvedPointerKey -Pointer $pointer
  }

  $sessionHeads = $null
  if ($IncludeSessionHeads -and (Test-Path -LiteralPath $sessionHeadsFile)) {
    $sessionHeads = Read-JsonFile -Path $sessionHeadsFile
  }

  Write-JsonResult @{
    ok = $true
    dryRun = [bool]$DryRun
    projectId = $ProjectId
    agentId = $AgentId
    pointerKey = $resolvedPointerKey
    pointerFile = (Get-ProjectReadPointerFile -Root $root -ProjectId $ProjectId -AgentId $AgentId -PointerKey $resolvedPointerKey)
    boundaryBlocked = [bool]$boundaryBlocked
    lossyBoundary = $lossyBoundary
    newestEntry = if ($newest.Count -gt 0) { $newest } else { $null }
    consumedEntryCount = @($unreadBeforeFilters).Count
    returnedEntryCount = @($returnedEntries).Count
    remainingUnreadCount = $remainingUnreadCount
    chunked = [bool]($remainingUnreadCount -gt 0)
    filters = @{
      otherAgentsOnly = [bool]$OtherAgentsOnly
      types = $normalizedTypes
      modeSignature = $modeSignature
    }
    unreadEntries = $returnedEntries
    streamFile = $streamFile
    sessionHeadsFile = $sessionHeadsFile
    sessionHeads = $sessionHeads
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
