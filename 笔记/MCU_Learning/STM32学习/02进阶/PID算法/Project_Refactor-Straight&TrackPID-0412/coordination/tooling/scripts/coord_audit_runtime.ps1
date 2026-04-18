[CmdletBinding()]
param(
  [string]$ProjectId,
  [switch]$Strict
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

function New-Finding {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Level,
    [Parameter(Mandatory = $true)]
    [string]$Code,
    [Parameter(Mandatory = $true)]
    [string]$Message,
    [string]$ProjectId = "",
    [string]$Path = ""
  )

  return @{
    level = $Level
    code = $Code
    message = $Message
    projectId = $ProjectId
    path = $Path
  }
}

function Add-Finding {
  param(
    [Parameter(Mandatory = $true)]
    [ref]$Collection,
    [Parameter(Mandatory = $true)]
    [hashtable]$Finding
  )

  $Collection.Value += ,$Finding
}

function Get-TextOrderIndex {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Text,
    [Parameter(Mandatory = $true)]
    [string]$Token
  )

  return $Text.IndexOf($Token, [System.StringComparison]::OrdinalIgnoreCase)
}

function Test-TokenOrder {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Text,
    [Parameter(Mandatory = $true)]
    [string]$Earlier,
    [Parameter(Mandatory = $true)]
    [string]$Later
  )

  $earlierIndex = Get-TextOrderIndex -Text $Text -Token $Earlier
  $laterIndex = Get-TextOrderIndex -Text $Text -Token $Later

  if ($earlierIndex -lt 0 -or $laterIndex -lt 0) {
    return $false
  }

  return ($earlierIndex -lt $laterIndex)
}

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $issues = @()
  $warnings = @()
  $projectSummaries = @()

  $topFiles = @(
    (Get-CoordinationAiEntryFile -Root $root),
    (Join-Path (Get-CoordinationRoot -Root $root) "START_HERE.md"),
    (Join-Path (Get-CoordinationRoot -Root $root) "README.md"),
    (Get-CoordinationActiveContextMarkdownFile -Root $root),
    (Get-CoordinationActiveContextJsonFile -Root $root)
  )
  foreach ($path in $topFiles) {
    if (-not (Test-Path -LiteralPath $path)) {
      Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "missing_top_file" -Message ("Missing coordination entry file: {0}" -f $path) -Path $path)
    }
  }

  $coordRegistry = Read-CoordinationRegistry -Root $root
  $activeContext = if (Test-Path -LiteralPath (Get-CoordinationActiveContextJsonFile -Root $root)) {
    Read-JsonFile -Path (Get-CoordinationActiveContextJsonFile -Root $root)
  }
  else {
    @{}
  }

  $projectIds = @()
  if (-not [string]::IsNullOrWhiteSpace($ProjectId)) {
    $projectIds = @($ProjectId)
  }
  else {
    foreach ($entry in (ConvertTo-ObjectArray -Value $coordRegistry["projects"])) {
      if ($entry -is [hashtable] -and $entry.ContainsKey("id") -and -not [string]::IsNullOrWhiteSpace([string]$entry["id"])) {
        $projectIds += [string]$entry["id"]
      }
    }

    if (@($projectIds).Count -eq 0) {
      $projectsRoot = Get-CoordinationProjectsRoot -Root $root
      if (Test-Path -LiteralPath $projectsRoot) {
        foreach ($dir in (Get-ChildItem -LiteralPath $projectsRoot -Directory -ErrorAction SilentlyContinue)) {
          if (Test-Path -LiteralPath (Join-Path $dir.FullName "project.json")) {
            $projectIds += $dir.Name
          }
        }
      }
    }
  }

  $projectIds = @($projectIds | Sort-Object -Unique)

  if (@($projectIds).Count -eq 0) {
    Add-Finding -Collection ([ref]$warnings) -Finding (New-Finding -Level "warning" -Code "no_projects" -Message "No active coordination project runtime was found under coordination/runtime/projects.")
  }

  $preferredProjectId = if ($activeContext.ContainsKey("preferredProjectId")) { [string]$activeContext["preferredProjectId"] } else { "" }
  if (-not [string]::IsNullOrWhiteSpace($preferredProjectId) -and ($preferredProjectId -notin $projectIds)) {
    Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "preferred_project_missing" -Message ("ACTIVE_CONTEXT.json points to missing project '{0}'." -f $preferredProjectId) -ProjectId $preferredProjectId)
  }

  foreach ($currentProjectId in $projectIds) {
    $projectPath = Get-ProjectCoordinationPath -Root $root -ProjectId $currentProjectId
    $projectSummary = @{
      projectId = $currentProjectId
      issueCount = 0
      warningCount = 0
      activeSessionCount = 0
    }

    if (-not (Test-Path -LiteralPath $projectPath)) {
      Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "missing_project_runtime" -Message ("Project runtime path does not exist: {0}" -f $projectPath) -ProjectId $currentProjectId -Path $projectPath)
      $projectSummaries += ,$projectSummary
      continue
    }

    $requiredProjectFiles = @(
      (Join-Path $projectPath "project.json"),
      (Join-Path $projectPath "README.md"),
      (Get-ProjectAiStartFile -Root $root -ProjectId $currentProjectId),
      (Get-ProjectReadmeIndexFile -Root $root -ProjectId $currentProjectId),
      (Get-ProjectArchitectureIndexFile -Root $root -ProjectId $currentProjectId),
      (Get-ProjectArchitectureTreeFile -Root $root -ProjectId $currentProjectId),
      (Get-ProjectLiveStateBoardFile -Root $root -ProjectId $currentProjectId),
      (Get-ProjectDigestLatestSummaryFile -Root $root -ProjectId $currentProjectId),
      (Get-ProjectRegistryFile -Root $root -ProjectId $currentProjectId),
      (Get-ProjectOnboardingPromptFile -Root $root -ProjectId $currentProjectId)
    )

    foreach ($path in $requiredProjectFiles) {
      if (-not (Test-Path -LiteralPath $path)) {
        Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "missing_project_file" -Message ("Missing project file: {0}" -f $path) -ProjectId $currentProjectId -Path $path)
      }
    }

    $legacyArchivePath = Join-Path (Get-ProjectChangelogPath -Root $root -ProjectId $currentProjectId) "L3_ARCHIVE"
    if (Test-Path -LiteralPath $legacyArchivePath) {
      Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "legacy_l3_archive" -Message "Legacy CHANGELOG_TIER/L3_ARCHIVE directory still exists; migrate it to L4_ARCHIVE." -ProjectId $currentProjectId -Path $legacyArchivePath)
    }

    $aiStartFile = Get-ProjectAiStartFile -Root $root -ProjectId $currentProjectId
    if (Test-Path -LiteralPath $aiStartFile) {
      $aiStartText = Get-Content -LiteralPath $aiStartFile -Raw
      if (-not $aiStartText.Contains("## Minimal Read Chain")) {
        Add-Finding -Collection ([ref]$warnings) -Finding (New-Finding -Level "warning" -Code "ai_start_old_format" -Message "AI_START.md does not expose the new minimal read chain section." -ProjectId $currentProjectId -Path $aiStartFile)
      }
      if (-not (Test-TokenOrder -Text $aiStartText -Earlier "context/LIVE_STATE_BOARD.md" -Later "README_INDEX.md")) {
        Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "ai_start_order" -Message "AI_START.md does not place LIVE_STATE_BOARD before README_INDEX." -ProjectId $currentProjectId -Path $aiStartFile)
      }
      if (-not (Test-TokenOrder -Text $aiStartText -Earlier "CHANGELOG_TIER/L3_DIGEST/latest_summary.md" -Later "README_INDEX.md")) {
        Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "ai_start_digest_order" -Message "AI_START.md does not place the digest head before README_INDEX." -ProjectId $currentProjectId -Path $aiStartFile)
      }
    }

    $readmeIndexFile = Get-ProjectReadmeIndexFile -Root $root -ProjectId $currentProjectId
    if (Test-Path -LiteralPath $readmeIndexFile) {
      $readmeIndexText = Get-Content -LiteralPath $readmeIndexFile -Raw
      if ($readmeIndexText.Contains('1. `README_INDEX.md`')) {
        Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "readme_index_self_reference" -Message "README_INDEX.md still self-references as the first read step." -ProjectId $currentProjectId -Path $readmeIndexFile)
      }
      if (-not $readmeIndexText.Contains("authoritative minimal start packet")) {
        Add-Finding -Collection ([ref]$warnings) -Finding (New-Finding -Level "warning" -Code "readme_index_old_contract" -Message "README_INDEX.md does not state that AI_START.md is the authoritative minimal packet." -ProjectId $currentProjectId -Path $readmeIndexFile)
      }
    }

    $onboardingPromptFile = Get-ProjectOnboardingPromptFile -Root $root -ProjectId $currentProjectId
    if (Test-Path -LiteralPath $onboardingPromptFile) {
      $onboardingText = Get-Content -LiteralPath $onboardingPromptFile -Raw
      if (-not $onboardingText.Contains('Treat `AI_START.md` as the authoritative project work packet')) {
        Add-Finding -Collection ([ref]$warnings) -Finding (New-Finding -Level "warning" -Code "onboarding_old_contract" -Message "AI_ONBOARDING_PROMPT.md does not mention AI_START.md as the authoritative work packet." -ProjectId $currentProjectId -Path $onboardingPromptFile)
      }
      if (-not (Test-TokenOrder -Text $onboardingText -Earlier "AI_START.md" -Later "README_INDEX.md")) {
        Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "onboarding_order" -Message "AI_ONBOARDING_PROMPT.md does not route through AI_START.md before README_INDEX.md." -ProjectId $currentProjectId -Path $onboardingPromptFile)
      }
    }

    $activeIndexFile = Get-ProjectActiveSessionsIndexFile -Root $root -ProjectId $currentProjectId
    $activeSessions = @()
    if (Test-Path -LiteralPath $activeIndexFile) {
      $activeIndex = Read-JsonFile -Path $activeIndexFile
      if ($activeIndex.ContainsKey("sessions")) {
        $activeSessions = ConvertTo-ObjectArray -Value $activeIndex["sessions"]
      }
    }
    $projectSummary["activeSessionCount"] = @($activeSessions).Count

    foreach ($session in $activeSessions) {
      if (-not ($session -is [hashtable])) {
        continue
      }

      $sessionId = if ($session.ContainsKey("sessionId")) { [string]$session["sessionId"] } else { "" }
      if ([string]::IsNullOrWhiteSpace($sessionId)) {
        Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "active_session_missing_id" -Message "An active session entry is missing sessionId." -ProjectId $currentProjectId -Path $activeIndexFile)
        continue
      }

      $sessionDeltaFile = Get-ProjectSessionDeltaFile -Root $root -ProjectId $currentProjectId -SessionId $sessionId -State "active"
      $thoughtLog = Get-ProjectSessionTriadFile -Root $root -ProjectId $currentProjectId -SessionId $sessionId -State "active" -Type "THOUGHT"
      $execLog = Get-ProjectSessionTriadFile -Root $root -ProjectId $currentProjectId -SessionId $sessionId -State "active" -Type "EXEC"
      $modLog = Get-ProjectSessionTriadFile -Root $root -ProjectId $currentProjectId -SessionId $sessionId -State "active" -Type "MOD"
      foreach ($path in @($sessionDeltaFile, $thoughtLog, $execLog, $modLog)) {
        if (-not (Test-Path -LiteralPath $path)) {
          Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "missing_session_file" -Message ("Missing active session file: {0}" -f $path) -ProjectId $currentProjectId -Path $path)
        }
      }

      $taskId = if ($session.ContainsKey("taskId")) { [string]$session["taskId"] } else { "" }
      if (-not [string]::IsNullOrWhiteSpace($taskId)) {
        $taskFile = Join-Path (Get-ProjectTasksPath -Root $root -ProjectId $currentProjectId) ($taskId + ".md")
        $lockFile = Join-Path (Get-ProjectLocksPath -Root $root -ProjectId $currentProjectId) ($taskId + ".json")
        foreach ($path in @($taskFile, $lockFile)) {
          if (-not (Test-Path -LiteralPath $path)) {
            Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "missing_task_contract" -Message ("Missing task or lock contract file for active session: {0}" -f $path) -ProjectId $currentProjectId -Path $path)
          }
        }
      }

      $sessionRegistry = Read-ProjectRegistry -Root $root -ProjectId $currentProjectId
      $registryEntry = $null
      foreach ($entry in (ConvertTo-ObjectArray -Value $sessionRegistry["sessions"])) {
        if ($entry -is [hashtable] -and $entry.ContainsKey("sessionId") -and ([string]$entry["sessionId"] -eq $sessionId)) {
          $registryEntry = $entry
          break
        }
      }
      if ($null -ne $registryEntry -and $registryEntry.ContainsKey("promptFile") -and -not [string]::IsNullOrWhiteSpace([string]$registryEntry["promptFile"])) {
        $promptFile = [string]$registryEntry["promptFile"]
        if (-not (Test-Path -LiteralPath $promptFile)) {
          Add-Finding -Collection ([ref]$issues) -Finding (New-Finding -Level "error" -Code "missing_prompt_file" -Message ("Registered prompt file is missing: {0}" -f $promptFile) -ProjectId $currentProjectId -Path $promptFile)
        }
      }
    }

    $projectIssueCount = @($issues | Where-Object { $_["projectId"] -eq $currentProjectId }).Count
    $projectWarningCount = @($warnings | Where-Object { $_["projectId"] -eq $currentProjectId }).Count
    $projectSummary["issueCount"] = $projectIssueCount
    $projectSummary["warningCount"] = $projectWarningCount
    $projectSummaries += ,$projectSummary
  }

  $ok = (@($issues).Count -eq 0)
  if ($Strict -and @($warnings).Count -gt 0) {
    $ok = $false
  }

  Write-JsonResult @{
    ok = $ok
    strict = [bool]$Strict
    checkedAt = Get-NowIso
    root = $root
    projectCount = @($projectIds).Count
    issueCount = @($issues).Count
    warningCount = @($warnings).Count
    projects = $projectSummaries
    issues = if (@($issues).Count -gt 0) { $issues } else { @() }
    warnings = if (@($warnings).Count -gt 0) { $warnings } else { @() }
    nextStep = if (@($issues).Count -eq 0 -and @($warnings).Count -eq 0) {
      "Runtime is structurally healthy."
    }
    elseif (@($issues).Count -eq 0) {
      "Warnings found. Consider refreshing the runtime with coord-init-project -Force or regenerating prompts/indexes."
    }
    else {
      "Fix the reported issues, then rerun coord-audit-runtime."
    }
  }

  if (-not $ok) {
    exit 1
  }
}
catch {
  Write-JsonResult @{
    ok = $false
    strict = [bool]$Strict
    error = $_.Exception.Message
  }
  exit 1
}
