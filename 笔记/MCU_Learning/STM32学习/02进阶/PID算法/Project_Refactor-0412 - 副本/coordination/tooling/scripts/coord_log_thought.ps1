[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectId,
  [Parameter(Mandatory = $true)]
  [string]$SessionId,
  [Parameter(Mandatory = $true)]
  [string]$AgentId,
  [Parameter(Mandatory = $true)]
  [string]$Content,
  [string[]]$RelatedFiles = @(),
  [string]$Status = "active",
  [string]$TaskId,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $sessionDeltaPath = Resolve-ProjectSessionDeltaPath -Root $root -ProjectId $ProjectId -SessionId $SessionId
  if ([string]::IsNullOrWhiteSpace($sessionDeltaPath)) {
    throw "Session delta was not found for session: $SessionId"
  }

  $sessionDelta = Read-JsonFile -Path $sessionDeltaPath
  if ([string]::IsNullOrWhiteSpace($TaskId) -and $sessionDelta.ContainsKey("taskId")) {
    $TaskId = [string]$sessionDelta["taskId"]
  }

  if ($DryRun) {
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      projectId = $ProjectId
      sessionId = $SessionId
      agentId = $AgentId
      taskId = $TaskId
      triadType = "THOUGHT"
      relatedFiles = ConvertTo-StringArray -Value $RelatedFiles
    }
    exit 0
  }

  $result = Write-TriadRecord -Root $root -ProjectId $ProjectId -SessionId $SessionId -AgentId $AgentId -TriadType "THOUGHT" -Content $Content -RelatedFiles $RelatedFiles -Status $Status -TaskId $TaskId -Metadata @{
    source = "coord-log-thought"
  }

  Write-JsonResult @{
    ok = $true
    dryRun = $false
    projectId = $ProjectId
    sessionId = $SessionId
    agentId = $AgentId
    taskId = $TaskId
    triadFile = $result["triadFile"]
    newestEntryFile = $result["newestEntryFile"]
    liveStateBoardFile = $result["liveStateBoardFile"]
    sessionDeltaFile = $result["sessionDeltaFile"]
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
