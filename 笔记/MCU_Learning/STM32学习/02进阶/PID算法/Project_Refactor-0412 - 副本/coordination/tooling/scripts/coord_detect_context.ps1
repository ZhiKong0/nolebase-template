[CmdletBinding()]
param(
  [string]$ProjectId
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $coordinationRoot = Get-CoordinationRoot -Root $root
  $aiEntryFile = Get-CoordinationAiEntryFile -Root $root
  $startHereFile = Join-Path $coordinationRoot "START_HERE.md"
  $activeContextMarkdownFile = Get-CoordinationActiveContextMarkdownFile -Root $root
  $activeContextJsonFile = Get-CoordinationActiveContextJsonFile -Root $root

  if (-not (Test-Path -LiteralPath $coordinationRoot) -or -not (Test-Path -LiteralPath $aiEntryFile)) {
    Write-JsonResult @{
      ok = $true
      root = $root
      coordinationPresent = $false
      runtimePresent = $false
      mode = "none"
      nextReadChain = @()
      notes = @(
        "No coordination package was detected in this workspace.",
        "Use the repository's normal onboarding flow."
      )
    }
    exit 0
  }

  if (-not (Test-Path -LiteralPath $activeContextJsonFile)) {
    Write-JsonResult @{
      ok = $true
      root = $root
      coordinationPresent = $true
      runtimePresent = $false
      mode = "package_only"
      aiEntryFile = $aiEntryFile
      startHereFile = $startHereFile
      nextReadChain = @($aiEntryFile)
      notes = @(
        "The coordination package exists, but no active runtime index was found yet.",
        "Read AI_ENTRY.md first, then continue with normal repo onboarding or initialize the runtime with coord-init-project."
      )
    }
    exit 0
  }

  $activeContext = Read-JsonFile -Path $activeContextJsonFile
  $projects = if ($activeContext.ContainsKey("projects")) { ConvertTo-ObjectArray -Value $activeContext["projects"] } else { @() }
  $preferredProjectId = if ($activeContext.ContainsKey("preferredProjectId")) { [string]$activeContext["preferredProjectId"] } else { "" }

  $selectedProject = $null
  if (-not [string]::IsNullOrWhiteSpace($ProjectId)) {
    foreach ($project in $projects) {
      if (($project -is [hashtable]) -and $project.ContainsKey("projectId") -and ([string]$project["projectId"] -eq $ProjectId)) {
        $selectedProject = $project
        break
      }
    }
  }

  if ($null -eq $selectedProject -and -not [string]::IsNullOrWhiteSpace($preferredProjectId)) {
    foreach ($project in $projects) {
      if (($project -is [hashtable]) -and $project.ContainsKey("projectId") -and ([string]$project["projectId"] -eq $preferredProjectId)) {
        $selectedProject = $project
        break
      }
    }
  }

  $nextReadChain = @($aiEntryFile, $activeContextMarkdownFile)
  $selectedProjectId = ""
  $projectSummary = $null
  if ($null -ne $selectedProject) {
    $selectedProjectId = [string]$selectedProject["projectId"]
    $aiStartFile = if ($selectedProject.ContainsKey("aiStartFile")) { [string]$selectedProject["aiStartFile"] } else { Get-ProjectAiStartFile -Root $root -ProjectId $selectedProjectId }
    $nextReadChain += @($aiStartFile)
    $projectSummary = @{
      projectId = $selectedProjectId
      projectName = if ($selectedProject.ContainsKey("projectName")) { [string]$selectedProject["projectName"] } else { $selectedProjectId }
      repoPath = if ($selectedProject.ContainsKey("repoPath")) { [string]$selectedProject["repoPath"] } else { "" }
      aiStartFile = $aiStartFile
      liveStateBoardFile = if ($selectedProject.ContainsKey("liveStateBoardFile")) { [string]$selectedProject["liveStateBoardFile"] } else { "" }
      sessionHeadsFile = if ($selectedProject.ContainsKey("sessionHeadsFile")) { [string]$selectedProject["sessionHeadsFile"] } else { "" }
      digestLatestFile = if ($selectedProject.ContainsKey("digestLatestFile")) { [string]$selectedProject["digestLatestFile"] } else { "" }
      streamNewestEntryFile = if ($selectedProject.ContainsKey("streamNewestEntryFile")) { [string]$selectedProject["streamNewestEntryFile"] } else { "" }
      readmeIndexFile = if ($selectedProject.ContainsKey("readmeIndexFile")) { [string]$selectedProject["readmeIndexFile"] } else { "" }
      activeSessionCount = if ($selectedProject.ContainsKey("activeSessionCount")) { [int]$selectedProject["activeSessionCount"] } else { 0 }
      promptFiles = if ($selectedProject.ContainsKey("promptFiles")) { ConvertTo-StringArray -Value $selectedProject["promptFiles"] } else { @() }
    }
  }

  $mode = if ($null -ne $selectedProject) { "runtime_active" } else { "runtime_index_only" }
  $notes = @(
    "Use coordination as the first shared-context layer before normal repo traversal.",
    "Read incrementally: AI_ENTRY -> ACTIVE_CONTEXT -> AI_START, then let AI_START unlock the next layer.",
    "Prefer latest shared state first: LIVE_STATE_BOARD, SESSION_HEADS, digest head, active session index, and newest stream entry before deeper expansion."
  )

  if ($null -eq $selectedProject) {
    $notes += "No preferred project runtime was selected from ACTIVE_CONTEXT. Inspect ACTIVE_CONTEXT.md and choose a project explicitly."
  }

  Write-JsonResult @{
    ok = $true
    root = $root
    coordinationPresent = $true
    runtimePresent = $true
    mode = $mode
    aiEntryFile = $aiEntryFile
    activeContextFile = $activeContextMarkdownFile
    activeContextJsonFile = $activeContextJsonFile
    preferredProjectId = $preferredProjectId
    selectedProjectId = $selectedProjectId
    nextReadChain = $nextReadChain
    project = $projectSummary
    notes = $notes
  }
}
catch {
  Write-JsonResult @{
    ok = $false
    error = $_.Exception.Message
  }
  exit 1
}
