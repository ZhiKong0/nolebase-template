---
file_type: project_overview
project_id: openclaw-runtime
write_mode: script_managed
last_updated: 2026-04-12T11:57:42.5185530+08:00
---
# openclaw-runtime Coordination

## AI Entry

New AIs should enter through:

1. `coordination/AI_ENTRY.md`
2. `coordination/runtime/ACTIVE_CONTEXT.md`
3. `AI_START.md`
4. follow `AI_START.md`

## Project Facts

- Project id: `openclaw-runtime`
- Repository root: `E:/OpenClaw/.openclaw/workspace`
- Project coordination path: `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime`
- Worktree root: `E:\OpenClaw\coordination\runtime\worktrees\openclaw-runtime`
- Default branch: `main`

## Runtime Planes

- Control plane: `README_INDEX.md`, `ARCHITECTURE_INDEX.md`, `ARCHITECTURE_TREE.json`, `BOUNDARIES/`
- Stream plane: `STREAM_LOG/`
- Change plane: `CHANGELOG_TIER/`
- Session and audit plane: `SESSIONS/`, `LOGS/`, `STAGING/`
- Shared context plane: `context/`, `tasks/`, `locks/`, `activity/`, `state/`

## Cold Start Rule

New AIs must start with `AI_START.md`. That file is the authoritative project work packet and routes them into `context/LIVE_STATE_BOARD.md`, `STREAM_LOG/SESSION_HEADS.json`, `CHANGELOG_TIER/L3_DIGEST/latest_summary.md`, and `README_INDEX.md` only when needed.

They are not allowed to scan business directories before finishing the indexed read chain defined there.

## Main Commands

Inject the portable package into a repository:

```powershell
coordination\bin\coord-install-package.cmd -TargetRepoPath .
```

Claim task, create worktree and session, and generate the exact AI prompt:

```powershell
coordination\bin\coord-claim-task.cmd -ProjectId openclaw-runtime -AgentName <agent> -TaskId <task> -Scope <path>
```

Regenerate the exact prompt if task metadata changes:

```powershell
coordination\bin\coord-generate-prompt.cmd -ProjectId openclaw-runtime -TaskId <task> -SessionId <session>
```

Resume a closed task:

```powershell
coordination\bin\coord-reopen-task.cmd -ProjectId openclaw-runtime -AgentName <agent> -TaskId <task> -Scope <path>
```

Hand an active task from one AI to another:

```powershell
coordination\bin\coord-handoff-task.cmd -ProjectId openclaw-runtime -TaskId <task> -ToAgentName <agent> -Summary "what to continue"
```

Log the triad during work:

```powershell
coordination\bin\coord-log-thought.cmd -ProjectId openclaw-runtime -SessionId <session> -AgentId <agent> -Content "intent"
coordination\bin\coord-log-operation.cmd -ProjectId openclaw-runtime -SessionId <session> -AgentId <agent> -ActionType read -TargetPath <path> -DeltaSummary "why"
coordination\bin\coord-log-mod.cmd -ProjectId openclaw-runtime -SessionId <session> -AgentId <agent> -Content "what changed" -RelatedFiles <path>
coordination\bin\coord-read-stream.cmd -ProjectId openclaw-runtime -AgentId <agent> -OtherAgentsOnly -IncludeSessionHeads
```

Close the task, release the worktree, and archive the session:

```powershell
coordination\bin\coord-close-task.cmd -ProjectId openclaw-runtime -AgentName <agent> -TaskId <task>
```

Before closing, make sure the worktree is committed or otherwise clean enough to remove safely.

Archive the project runtime when collaboration is finished:

```powershell
coordination\bin\coord-archive-project.cmd -ProjectId openclaw-runtime
```
