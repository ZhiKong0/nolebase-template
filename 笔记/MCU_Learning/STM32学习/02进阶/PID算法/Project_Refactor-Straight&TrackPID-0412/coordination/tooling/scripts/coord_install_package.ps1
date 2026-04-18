[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$TargetRepoPath,
  [string]$ProjectName,
  [string]$ProjectId,
  [switch]$SkipProjectInit,
  [switch]$SkipRootAgents,
  [switch]$Force,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\common.ps1")

function Merge-ManagedAgentsBlock {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ExistingContent,
    [Parameter(Mandatory = $true)]
    [string]$ManagedBlock
  )

  $startMarker = '<!-- coordination-managed:start -->'
  $endMarker = '<!-- coordination-managed:end -->'
  if ($ExistingContent.Contains($startMarker) -and $ExistingContent.Contains($endMarker)) {
    return [regex]::Replace($ExistingContent, '(?s)<!-- coordination-managed:start -->.*?<!-- coordination-managed:end -->', $ManagedBlock.Trim())
  }

  $trimmed = $ExistingContent.TrimEnd()
  if ([string]::IsNullOrWhiteSpace($trimmed)) {
    return $ManagedBlock.Trim() + [Environment]::NewLine
  }

  return $trimmed + [Environment]::NewLine + [Environment]::NewLine + $ManagedBlock.Trim() + [Environment]::NewLine
}

try {
  $root = Get-OpenClawRoot -ScriptRoot $PSScriptRoot
  $sourceCoordinationRoot = Get-CoordinationRoot -Root $root
  if (-not (Test-Path -LiteralPath $TargetRepoPath)) {
    throw "Target path does not exist: $TargetRepoPath"
  }

  $resolvedTargetPath = (Resolve-Path -LiteralPath $TargetRepoPath).Path
  $targetRoot = $resolvedTargetPath
  $isGitRepo = $true
  try {
    $targetRoot = Resolve-GitRepositoryRoot -Path $resolvedTargetPath
  }
  catch {
    $isGitRepo = $false
    $targetRoot = $resolvedTargetPath
  }

  $targetCoordinationRoot = Join-Path $targetRoot "coordination"
  $targetRegistryPath = Join-Path $targetCoordinationRoot "system\state\registry.json"
  $rootAgentsPath = Join-Path $targetRoot "AGENTS.md"
  $managedAgentsTemplatePath = Join-Path $sourceCoordinationRoot "system\templates\package\AGENTS_MANAGED_BLOCK.md"
  $agentsFileTemplatePath = Join-Path $sourceCoordinationRoot "system\templates\package\AGENTS_FILE.md"
  $managedAgentsBlock = Get-Content -LiteralPath $managedAgentsTemplatePath -Raw
  $projectInitRequested = -not $SkipProjectInit
  $shouldInitializeProject = $projectInitRequested -and $isGitRepo
  $resolvedProjectName = if (-not [string]::IsNullOrWhiteSpace($ProjectName)) { $ProjectName } else { Split-Path -Leaf $targetRoot }
  $resolvedProjectId = if (-not [string]::IsNullOrWhiteSpace($ProjectId)) { $ProjectId } else { ConvertTo-Slug -Text $resolvedProjectName }
  $runtimeProjectPath = Join-Path $targetCoordinationRoot ("runtime\projects\" + $resolvedProjectId)
  $runtimeAlreadyExists = Test-Path -LiteralPath $runtimeProjectPath

  $plannedTopFiles = @(
    "AI_ENTRY.md",
    "README.md",
    "START_HERE.md",
    "ARCHITECTURE.md",
    "MULTI_AGENT_PROTOCOL.md"
  )
  $plannedDirs = @("bin", "tooling", "system")

  if ($DryRun) {
    Write-JsonResult @{
      ok = $true
      dryRun = $true
      targetRoot = $targetRoot
      isGitRepo = $isGitRepo
      targetCoordinationRoot = $targetCoordinationRoot
      topFiles = $plannedTopFiles
      directories = $plannedDirs
      rootAgentsPath = if ($SkipRootAgents) { $null } else { $rootAgentsPath }
      projectInitRequested = $projectInitRequested
      projectInitialized = ($shouldInitializeProject -and (-not $runtimeAlreadyExists -or $Force))
      projectId = if ($shouldInitializeProject) { $resolvedProjectId } else { $null }
      projectName = if ($shouldInitializeProject) { $resolvedProjectName } else { $null }
      warnings = @(
        if (-not $isGitRepo -and $projectInitRequested) { "Target path is not inside a git repository. Package copy can proceed, but project runtime initialization will be skipped." }
        elseif ($runtimeAlreadyExists -and -not $Force) { "Project runtime already exists and will not be re-initialized without -Force." }
      ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    }
    exit 0
  }

  $existingRegistry = if (Test-Path -LiteralPath $targetRegistryPath) { Read-JsonFile -Path $targetRegistryPath } else { $null }
  Ensure-Directory -Path $targetCoordinationRoot

  foreach ($file in $plannedTopFiles) {
    Copy-Item -LiteralPath (Join-Path $sourceCoordinationRoot $file) -Destination (Join-Path $targetCoordinationRoot $file) -Force
  }
  foreach ($dir in $plannedDirs) {
    Copy-Item -LiteralPath (Join-Path $sourceCoordinationRoot $dir) -Destination $targetCoordinationRoot -Recurse -Force
  }

  foreach ($path in @(
    (Join-Path $targetCoordinationRoot "runtime"),
    (Join-Path $targetCoordinationRoot "runtime\projects"),
    (Join-Path $targetCoordinationRoot "runtime\worktrees"),
    (Join-Path $targetCoordinationRoot "runtime\archive"),
    (Join-Path $targetCoordinationRoot "runtime\archive\projects"),
    (Join-Path $targetCoordinationRoot "runtime\archive\worktrees")
  )) {
    Ensure-Directory -Path $path
  }
  Copy-Item -LiteralPath (Join-Path $sourceCoordinationRoot "runtime\README.md") -Destination (Join-Path $targetCoordinationRoot "runtime\README.md") -Force

  if ($null -ne $existingRegistry -and ($existingRegistry -is [hashtable])) {
    Write-JsonFile -Path $targetRegistryPath -Object $existingRegistry
  }
  else {
    Write-JsonFile -Path $targetRegistryPath -Object (New-CoordinationRegistry)
  }

  $agentsUpdated = $false
  if (-not $SkipRootAgents) {
    if (Test-Path -LiteralPath $rootAgentsPath) {
      $existingAgents = Get-Content -LiteralPath $rootAgentsPath -Raw
      $mergedAgents = Merge-ManagedAgentsBlock -ExistingContent $existingAgents -ManagedBlock $managedAgentsBlock
      Write-TextFile -Path $rootAgentsPath -Content $mergedAgents
    }
    else {
      Write-TextFile -Path $rootAgentsPath -Content (Get-Content -LiteralPath $agentsFileTemplatePath -Raw)
    }
    $agentsUpdated = $true
  }

  $initializedProject = $false
  $initResult = $null
  $warnings = @()
  if ($projectInitRequested) {
    if (-not $isGitRepo) {
      $warnings += "Target path is not inside a git repository. Package copy completed, but project runtime initialization was skipped."
    }
    elseif ($runtimeAlreadyExists -and -not $Force) {
      $warnings += "Project runtime already exists. Package files were updated, but coord-init-project was skipped."
    }
    else {
      $initArgs = @(
        "-ProjectName", $resolvedProjectName,
        "-ProjectId", $resolvedProjectId,
        "-RepoPath", $targetRoot
      )
      if ($Force) { $initArgs += "-Force" }
      $initText = (& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $targetCoordinationRoot "bin\coord-init-project.ps1") @initArgs | Out-String).Trim()
      if ([string]::IsNullOrWhiteSpace($initText)) {
        throw "coord-init-project returned empty output for target repo: $targetRoot"
      }
      $initResult = ConvertTo-PlainObject ($initText | ConvertFrom-Json)
      if (-not $initResult["ok"]) {
        throw [string]$initResult["error"]
      }
      $initializedProject = $true
    }
  }

  Update-CoordinationRuntimeIndexes -Root $targetRoot

  Write-JsonResult @{
    ok = $true
    dryRun = $false
    targetRoot = $targetRoot
    isGitRepo = $isGitRepo
    targetCoordinationRoot = $targetCoordinationRoot
    rootAgentsPath = if ($SkipRootAgents) { $null } else { $rootAgentsPath }
    rootAgentsUpdated = $agentsUpdated
    projectInitialized = $initializedProject
    projectId = if ($initializedProject -or $runtimeAlreadyExists) { $resolvedProjectId } else { $null }
    projectName = if ($initializedProject -or $runtimeAlreadyExists) { $resolvedProjectName } else { $null }
    aiEntryFile = (Get-CoordinationAiEntryFile -Root $targetRoot)
    activeContextFile = (Get-CoordinationActiveContextMarkdownFile -Root $targetRoot)
    onboardingPrompt = if ($initializedProject -and $initResult.ContainsKey("onboardingPrompt")) { [string]$initResult["onboardingPrompt"] } else { $null }
    warnings = if ($warnings.Count -gt 0) { $warnings } else { $null }
    nextStep = if ($initializedProject) { "Use coord-claim-task in the target repo to open the first active AI session." } else { "Open the target repo, then run coordination\\bin\\coord-init-project.cmd if you want a live project runtime." }
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
