[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$ProjectName,
  [Parameter(Mandatory = $true)]
  [string]$RepoPath,
  [string]$ProjectId,
  [string]$DefaultBranch,
  [string]$WorktreeRoot,
  [switch]$Force,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $repoRoot = Resolve-GitRepositoryRoot -Path $RepoPath

  $resolvedProjectId = if (-not [string]::IsNullOrWhiteSpace($ProjectId)) {
    if ($ProjectId -notmatch "^[A-Za-z0-9._-]+$") {
      throw "ProjectId must match ^[A-Za-z0-9._-]+$."
    }
    $ProjectId
  }
  else {
    ConvertTo-Slug -Text $ProjectName
  }

  $coordinationPath = Get-ProjectCoordinationPath -Root $root -ProjectId $resolvedProjectId
  $contextPath = Get-ProjectContextPath -Root $root -ProjectId $resolvedProjectId
  $tasksPath = Get-ProjectTasksPath -Root $root -ProjectId $resolvedProjectId
  $locksPath = Get-ProjectLocksPath -Root $root -ProjectId $resolvedProjectId
  $activityPath = Get-ProjectActivityPath -Root $root -ProjectId $resolvedProjectId
  $statePath = Get-ProjectStatePath -Root $root -ProjectId $resolvedProjectId
  $boundariesPath = Get-ProjectBoundariesPath -Root $root -ProjectId $resolvedProjectId
  $streamLogPath = Get-ProjectStreamLogPath -Root $root -ProjectId $resolvedProjectId
  $streamReadPointersPath = Get-ProjectStreamReadPointersPath -Root $root -ProjectId $resolvedProjectId
  $l0Path = Get-ProjectChangelogLevelPath -Root $root -ProjectId $resolvedProjectId -Level "L0_BASE"
  $l1Path = Get-ProjectChangelogLevelPath -Root $root -ProjectId $resolvedProjectId -Level "L1_ACTIVE"
  $l2Path = Get-ProjectChangelogLevelPath -Root $root -ProjectId $resolvedProjectId -Level "L2_RECENT"
  $l3Path = Get-ProjectDigestPath -Root $root -ProjectId $resolvedProjectId
  $l4Path = Get-ProjectChangelogLevelPath -Root $root -ProjectId $resolvedProjectId -Level "L4_ARCHIVE"
  $logsPath = Get-ProjectLogsPath -Root $root -ProjectId $resolvedProjectId
  $operationLogsPath = Get-ProjectOperationLogsPath -Root $root -ProjectId $resolvedProjectId
  $conflictLogsPath = Get-ProjectConflictLogsPath -Root $root -ProjectId $resolvedProjectId
  $activeSessionsPath = Get-ProjectActiveSessionsPath -Root $root -ProjectId $resolvedProjectId
  $archivedSessionsPath = Get-ProjectArchivedSessionsPath -Root $root -ProjectId $resolvedProjectId
  $stagingPath = Get-ProjectStagingPath -Root $root -ProjectId $resolvedProjectId
  $patchStagingPath = Get-ProjectPatchStagingPath -Root $root -ProjectId $resolvedProjectId
  $readmeIndexFile = Get-ProjectReadmeIndexFile -Root $root -ProjectId $resolvedProjectId
  $architectureIndexFile = Get-ProjectArchitectureIndexFile -Root $root -ProjectId $resolvedProjectId
  $architectureTreeFile = Get-ProjectArchitectureTreeFile -Root $root -ProjectId $resolvedProjectId
  $baselineStateFile = Get-ProjectBaselineStateFile -Root $root -ProjectId $resolvedProjectId
  $activeSessionsIndexFile = Get-ProjectActiveSessionsIndexFile -Root $root -ProjectId $resolvedProjectId
  $recentIndexFile = Get-ProjectRecentIndexFile -Root $root -ProjectId $resolvedProjectId
  $digestIndexFile = Get-ProjectDigestIndexFile -Root $root -ProjectId $resolvedProjectId
  $digestLatestSummaryFile = Get-ProjectDigestLatestSummaryFile -Root $root -ProjectId $resolvedProjectId
  $archiveIndexFile = Get-ProjectArchiveIndexFile -Root $root -ProjectId $resolvedProjectId
  $streamNewestEntryFile = Get-ProjectStreamNewestEntryFile -Root $root -ProjectId $resolvedProjectId
  $streamTailFile = Get-ProjectStreamTailFile -Root $root -ProjectId $resolvedProjectId
  $liveStateBoardFile = Get-ProjectLiveStateBoardFile -Root $root -ProjectId $resolvedProjectId
  $conflictMarkdownFile = Get-ProjectConflictMarkdownFile -Root $root -ProjectId $resolvedProjectId
  $resolvedWorktreeRoot = if (-not [string]::IsNullOrWhiteSpace($WorktreeRoot)) {
    $WorktreeRoot
  }
  else {
    Get-ProjectWorktreeRoot -Root $root -ProjectId $resolvedProjectId
  }

  $resolvedDefaultBranch = if (-not [string]::IsNullOrWhiteSpace($DefaultBranch)) {
    $DefaultBranch
  }
  else {
    Get-DefaultGitBranch -RepoPath $repoRoot
  }

  if ((Test-Path -LiteralPath $coordinationPath) -and -not $Force) {
    throw "Project coordination path already exists: $coordinationPath. Use -Force to overwrite files."
  }

  $today = Get-Date -Format "yyyy-MM-dd"
  $now = Get-NowIso
  $baselineVersion = "L0-{0}" -f (Get-Date -Format "yyyyMMdd-HHmmss")
  $recentRetentionHours = Get-RecentSessionRetentionHours
  $recentRetentionCount = Get-RecentSessionRetentionCount
  $tokens = @{
    PROJECT_NAME = $ProjectName
    PROJECT_ID = $resolvedProjectId
    REPO_PATH = $repoRoot
    REPO_PATH_JSON = $repoRoot.Replace("\", "\\")
    COORDINATION_PATH = $coordinationPath
    COORDINATION_PATH_JSON = $coordinationPath.Replace("\", "\\")
    WORKTREE_ROOT = $resolvedWorktreeRoot
    DEFAULT_BRANCH = $resolvedDefaultBranch
    TODAY = $today
    NOW = $now
    BASELINE_VERSION = $baselineVersion
    RECENT_RETENTION_HOURS = $recentRetentionHours
    RECENT_RETENTION_COUNT = $recentRetentionCount
  }

  $plannedFiles = @(
    (Join-Path $coordinationPath "README.md"),
    (Get-ProjectAiStartFile -Root $root -ProjectId $resolvedProjectId),
    $readmeIndexFile,
    $architectureIndexFile,
    $architectureTreeFile,
    (Join-Path $coordinationPath "project.json"),
    (Join-Path $contextPath "BOARD.md"),
    (Join-Path $contextPath "HANDOFF.md"),
    (Join-Path $contextPath "DECISIONS.md"),
    $liveStateBoardFile,
    (Join-Path $contextPath "AI_ONBOARDING_PROMPT.md"),
    (Join-Path $tasksPath "README.md"),
    (Join-Path $tasksPath "TASK_TEMPLATE.md"),
    (Join-Path $locksPath "README.md"),
    (Join-Path $locksPath "LOCK_TEMPLATE.json"),
    (Join-Path $activityPath "README.md"),
    (Join-Path $boundariesPath "PATH_POLICY.md"),
    (Join-Path $streamLogPath "README.md"),
    $streamNewestEntryFile,
    $streamTailFile,
    (Join-Path $streamReadPointersPath "README.md"),
    (Join-Path $l0Path "README.md"),
    $baselineStateFile,
    (Join-Path $l1Path "README.md"),
    $activeSessionsIndexFile,
    (Join-Path $l2Path "README.md"),
    $recentIndexFile,
    (Join-Path $l3Path "README.md"),
    $digestIndexFile,
    $digestLatestSummaryFile,
    (Join-Path $l4Path "README.md"),
    $archiveIndexFile,
    (Join-Path $logsPath "AGENT_LOG_SCHEMA.yaml"),
    $conflictMarkdownFile,
    (Join-Path $operationLogsPath "README.md"),
    (Join-Path $conflictLogsPath "README.md"),
    (Join-Path $activeSessionsPath "README.md"),
    (Join-Path $archivedSessionsPath "README.md"),
    (Join-Path $stagingPath "README.md"),
    (Join-Path $patchStagingPath "README.md"),
    (Join-Path $statePath "registry.json")
  )

  if ($DryRun) {
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      projectId = $resolvedProjectId
      projectName = $ProjectName
      repoPath = $repoRoot
      coordinationPath = $coordinationPath
      worktreeRoot = $resolvedWorktreeRoot
      defaultBranch = $resolvedDefaultBranch
      baselineVersion = $baselineVersion
      files = $plannedFiles
      dispatchedAt = $now
    }
    exit 0
  }

  foreach ($path in @(
    $coordinationPath,
    $contextPath,
    $tasksPath,
    $locksPath,
    $activityPath,
    $statePath,
    $boundariesPath,
    $streamLogPath,
    $streamReadPointersPath,
    $l0Path,
    $l1Path,
    $l2Path,
    $l3Path,
    $l4Path,
    $logsPath,
    $operationLogsPath,
    $conflictLogsPath,
    $activeSessionsPath,
    $archivedSessionsPath,
    $stagingPath,
    $patchStagingPath,
    $resolvedWorktreeRoot
  )) {
    Ensure-Directory -Path $path
  }

  foreach ($templateFile in @(
    "README.md",
    "README_INDEX.md",
    "ARCHITECTURE_INDEX.md",
    "BOUNDARIES\PATH_POLICY.md",
    "STREAM_LOG\README.md",
    "STREAM_LOG\READ_POINTERS\README.md",
    "CHANGELOG_TIER\L0_BASE\README.md",
    "CHANGELOG_TIER\L1_ACTIVE\README.md",
    "CHANGELOG_TIER\L2_RECENT\README.md",
    "CHANGELOG_TIER\L3_DIGEST\README.md",
    "CHANGELOG_TIER\L3_DIGEST\latest_summary.md",
    "CHANGELOG_TIER\L4_ARCHIVE\README.md",
    "LOGS\AGENT_LOG_SCHEMA.yaml",
    "LOGS\CONFLICT_LOG.md",
    "LOGS\operations\README.md",
    "LOGS\conflicts\README.md",
    "SESSIONS\active\README.md",
    "SESSIONS\archive\README.md",
    "STAGING\README.md",
    "STAGING\patches\README.md",
    "context\BOARD.md",
    "context\HANDOFF.md",
    "context\DECISIONS.md",
    "context\LIVE_STATE_BOARD.md",
    "context\AI_ONBOARDING_PROMPT.md",
    "tasks\TASK_TEMPLATE.md",
    "tasks\README.md",
    "locks\README.md",
    "activity\README.md"
  )) {
    $content = Apply-Template -Template (Read-TemplateFile -Root $root -RelativePath $templateFile) -Tokens $tokens
    Write-TextFile -Path (Join-Path $coordinationPath $templateFile) -Content $content
  }

  $lockTemplateContent = Get-Content -LiteralPath (Join-Path (Get-CoordinationSystemRoot -Root $root) "templates\project\locks\LOCK_TEMPLATE.json") -Raw
  Write-TextFile -Path (Join-Path $locksPath "LOCK_TEMPLATE.json") -Content $lockTemplateContent

  $projectObject = @{
    version = 2
    id = $resolvedProjectId
    name = $ProjectName
    status = "active"
    repoPath = $repoRoot
    coordinationPath = $coordinationPath
    worktreeRoot = $resolvedWorktreeRoot
    defaultBranch = $resolvedDefaultBranch
    baselineVersion = $baselineVersion
    createdAt = $now
    updatedAt = $now
  }
  Write-JsonFile -Path (Join-Path $coordinationPath "project.json") -Object $projectObject

  $architectureTree = @{
    version = 1
    generatedAt = $now
    projectId = $resolvedProjectId
    projectName = $ProjectName
    baselineVersion = $baselineVersion
    readPriorityChain = @(
      "context/LIVE_STATE_BOARD.md",
      "CHANGELOG_TIER/L3_DIGEST/latest_summary.md",
      "CHANGELOG_TIER/L1_ACTIVE/ACTIVE_SESSIONS.json",
      "STREAM_LOG/NEWEST_ENTRY.json",
      "tasks/TASK-xxx.md",
      "locks/TASK-xxx.json",
      "SESSIONS/active/<session-id>/SESSION_DELTA.json",
      "SESSIONS/active/<session-id>/AI_PROMPT.md",
      "README_INDEX.md",
      "ARCHITECTURE_TREE.json",
      "ARCHITECTURE_INDEX.md",
      "CHANGELOG_TIER/L2_RECENT/RECENT_INDEX.json",
      "CHANGELOG_TIER/L0_BASE/BASELINE_STATE.json"
    )
    retention = @{
      streamTailEntries = Get-StreamTailSize
      l2RecentHours = $recentRetentionHours
      l2RecentCount = $recentRetentionCount
      digestHeadEntries = Get-DigestSessionRetentionCount
    }
    zones = @(
      @{
        name = "control_plane"
        paths = @("README_INDEX.md", "ARCHITECTURE_INDEX.md", "ARCHITECTURE_TREE.json", "BOUNDARIES/")
        mode = "read_only_for_task_ai"
      },
      @{
        name = "stream_plane"
        paths = @("STREAM_LOG/")
        mode = "bounded_incremental_read"
      },
      @{
        name = "change_plane"
        paths = @("CHANGELOG_TIER/L0_BASE", "CHANGELOG_TIER/L1_ACTIVE", "CHANGELOG_TIER/L2_RECENT", "CHANGELOG_TIER/L3_DIGEST", "CHANGELOG_TIER/L4_ARCHIVE")
        mode = "tiered_read"
      },
      @{
        name = "session_plane"
        paths = @("SESSIONS/", "LOGS/", "STAGING/")
        mode = "session_scoped"
      },
      @{
        name = "shared_context"
        paths = @("context/", "tasks/", "locks/", "activity/", "state/")
        mode = "task_scoped"
      },
      @{
        name = "execution_plane"
        paths = @("coordination/runtime/worktrees/{project-id}")
        mode = "isolated_checkout"
      }
    )
  }
  Write-JsonFile -Path $architectureTreeFile -Object $architectureTree

  $baselineState = @{
    version = 1
    baselineVersion = $baselineVersion
    createdAt = $now
    projectId = $resolvedProjectId
    repoPath = $repoRoot
    coordinationPath = $coordinationPath
    defaultBranch = $resolvedDefaultBranch
    worktreeRoot = $resolvedWorktreeRoot
    readPriorityChain = $architectureTree["readPriorityChain"]
    mutableZones = @("context/", "tasks/", "locks/", "activity/", "STREAM_LOG/", "SESSIONS/active/", "CHANGELOG_TIER/L1_ACTIVE/", "CHANGELOG_TIER/L3_DIGEST/", "LOGS/operations/", "LOGS/conflicts/", "STAGING/patches/")
    blockedReadZones = @("CHANGELOG_TIER/L4_ARCHIVE/")
  }
  Write-JsonFile -Path $baselineStateFile -Object $baselineState

  Write-JsonFile -Path $streamNewestEntryFile -Object @{
    version = 1
    updatedAt = $now
    entry_id = $null
    session_id = $null
    agent_id = $null
    type = $null
    status = $null
    content_preview = ""
    related_files = @()
  }
  Write-JsonFile -Path $streamTailFile -Object @{
    version = 1
    updatedAt = $now
    maxEntries = Get-StreamTailSize
    entries = @()
  }
  Write-JsonFile -Path $activeSessionsIndexFile -Object @{
    version = 1
    updatedAt = $now
    sessions = @()
  }
  Write-JsonFile -Path $recentIndexFile -Object @{
    version = 1
    updatedAt = $now
    retentionHours = $recentRetentionHours
    retentionCount = $recentRetentionCount
    sessions = @()
  }
  Write-JsonFile -Path $digestIndexFile -Object @{
    version = 1
    updatedAt = $now
    latestDigestId = $null
    digests = @()
  }
  Write-JsonFile -Path $archiveIndexFile -Object @{
    version = 1
    updatedAt = $now
    sessions = @()
  }

  Write-JsonFile -Path (Get-ProjectRegistryFile -Root $root -ProjectId $resolvedProjectId) -Object @{
    version = 2
    projectId = $resolvedProjectId
    status = "active"
    repoPath = $repoRoot
    coordinationPath = $coordinationPath
    worktreeRoot = $resolvedWorktreeRoot
    baselineVersion = $baselineVersion
    updatedAt = $now
    tasks = @()
    locks = @()
    sessions = @()
    tiers = @{
      l1Active = @()
      l2Recent = @()
      l3Digest = @()
      l4Archive = @()
    }
  }

  Upsert-CoordinationProject -Root $root -Project $projectObject
  Update-CoordinationRuntimeIndexes -Root $root

  Write-JsonResult @{
    ok = $true
    dryRun = $false
    projectId = $resolvedProjectId
    projectName = $ProjectName
    repoPath = $repoRoot
    coordinationPath = $coordinationPath
    worktreeRoot = $resolvedWorktreeRoot
    defaultBranch = $resolvedDefaultBranch
    baselineVersion = $baselineVersion
    aiStart = (Get-ProjectAiStartFile -Root $root -ProjectId $resolvedProjectId)
    activeContext = (Get-CoordinationActiveContextMarkdownFile -Root $root)
    readmeIndex = $readmeIndexFile
    architectureTree = $architectureTreeFile
    onboardingPrompt = (Get-ProjectOnboardingPromptFile -Root $root -ProjectId $resolvedProjectId)
    dispatchedAt = $now
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
