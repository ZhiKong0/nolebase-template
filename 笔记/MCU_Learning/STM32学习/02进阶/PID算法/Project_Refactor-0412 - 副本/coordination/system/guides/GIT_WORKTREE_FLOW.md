# Git Worktree Flow

## Goal

Run multiple AIs against one repository while keeping each coding task isolated in its own checkout and tied to its own session delta.

## Workflow

### 1. Register the Repository

```powershell
coordination\bin\coord-install-package.cmd -TargetRepoPath .
coordination\bin\coord-init-project.cmd -ProjectName my-app -RepoPath .
```

### 2. Create One Worktree and Session Per AI Task

```powershell
coordination\bin\coord-claim-task.cmd -ProjectId my-app -AgentName codex-a -TaskId TASK-001 -Scope src/app
```

This creates:

- a branch such as `ai/codex-a/task-001`
- a worktree such as `coordination/runtime/worktrees/my-app/codex-a-task-001`
- `SESSIONS/active/<session-id>/SESSION_DELTA.json`
- `SESSIONS/active/<session-id>/AI_PROMPT.md`
- `CHANGELOG_TIER/L1_ACTIVE/<session-id>.json`
- a task file and lock file

### 3. Brief the AI

Prefer the generated session prompt:

- `coordination\runtime\projects\my-app\SESSIONS\active\<session-id>\AI_PROMPT.md`

Fallback if a task is not yet claimed:

- `coordination\runtime\projects\my-app\context\AI_ONBOARDING_PROMPT.md`

### 4. Require Structured Logging

Before active work, use the triad:

```powershell
coordination\bin\coord-log-thought.cmd -ProjectId my-app -SessionId <session> -AgentId codex-a -Content "Inspect entrypoint and decide the first change"
coordination\bin\coord-log-operation.cmd -ProjectId my-app -SessionId <session> -AgentId codex-a -ActionType read -TargetPath src/app.ts -DeltaSummary "Inspect entrypoint"
coordination\bin\coord-log-mod.cmd -ProjectId my-app -SessionId <session> -AgentId codex-a -Content "Updated src/app.ts" -RelatedFiles src/app.ts
coordination\bin\coord-read-stream.cmd -ProjectId my-app -AgentId codex-a
```

### 5. Release the Worktree

```powershell
coordination\bin\coord-close-task.cmd -ProjectId my-app -TaskId TASK-001 -AgentName codex-a
```

On release, the session is moved out of `L1_ACTIVE` and summarized into `L2_RECENT`.
Before release, make sure the worktree is committed or otherwise clean enough to remove safely.

### 6. Archive the Runtime When Collaboration Stops

```powershell
coordination\bin\coord-archive-project.cmd -ProjectId my-app
```

This moves the project runtime and retired worktree tree under `coordination/runtime/archive/`.

### 7. Resume or Hand Off Work

Resume a closed task:

```powershell
coordination\bin\coord-reopen-task.cmd -ProjectId my-app -AgentName codex-b -TaskId TASK-001 -Scope src/app
```

Hand the baton directly to another active AI:

```powershell
coordination\bin\coord-handoff-task.cmd -ProjectId my-app -TaskId TASK-001 -ToAgentName codex-b -Summary "Continue the current refactor"
```
