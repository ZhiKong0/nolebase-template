# Coordination

This folder is a portable multi-AI collaboration package.

Its job is to make multiple AIs behave like they share one structured working memory, while each AI still edits code inside an isolated `git worktree`.

## AI-First Entry

If you are an AI entering this repository, read in this order:

1. `AI_ENTRY.md`
2. `runtime/ACTIVE_CONTEXT.md`
3. `runtime/projects/<project-id>/AI_START.md`
4. Follow that project's `AI_START.md` exactly

## Package Split

- `AI_ENTRY.md`: the single AI-first entrypoint
- `bin/`: portable command entrypoints
- `tooling/`: implementation used by the entrypoints
- `system/`: reusable prompts, guides, templates, and package state
- `runtime/`: repo-local coordination state generated from this package

## Runtime Split

- `runtime/ACTIVE_CONTEXT.md`: dynamic project chooser for AIs
- `runtime/ACTIVE_CONTEXT.json`: machine-readable active context index
- `runtime/projects/`: one coordination runtime per repository
- `runtime/worktrees/`: one isolated checkout per active AI task
- `runtime/archive/`: archived project runtimes and retired worktree trees

Inside each project runtime, the structure is further split into:

- AI entry plane: `AI_START.md`
- control plane: `README_INDEX.md`, `ARCHITECTURE_INDEX.md`, `ARCHITECTURE_TREE.json`, `BOUNDARIES/`
- stream plane: `STREAM_LOG/`, `READ_POINTERS/`
- change plane: `CHANGELOG_TIER/L0_BASE`, `L1_ACTIVE`, `L2_RECENT`, `L3_DIGEST`, `L4_ARCHIVE`
- session plane: `SESSIONS/active/<session-id>/SESSION_DELTA.json`, `THOUGHT.jsonl`, `EXEC.jsonl`, `MOD.jsonl`
- audit plane: `LOGS/operations/`, `LOGS/conflicts/`, `LOGS/CONFLICT_LOG.md`, `STAGING/`
- shared context plane: `context/`, `tasks/`, `locks/`, `activity/`, `state/`

## First Read Path

1. `AI_ENTRY.md`
2. `runtime/ACTIVE_CONTEXT.md`
3. `runtime/projects/<project-id>/AI_START.md`
4. `AI_START.md` then routes the AI into `context/LIVE_STATE_BOARD.md`
5. `AI_START.md` then routes the AI into `CHANGELOG_TIER/L3_DIGEST/latest_summary.md`
6. `AI_START.md` then decides whether `README_INDEX.md` is needed

## Main Commands

Use the self-contained entrypoints in this folder:

- `coordination\bin\coord-install-package.cmd`
- `coordination\bin\coord-claim-task.cmd`
- `coordination\bin\coord-reopen-task.cmd`
- `coordination\bin\coord-handoff-task.cmd`
- `coordination\bin\coord-generate-prompt.cmd`
- `coordination\bin\coord-close-task.cmd`
- `coordination\bin\coord-archive-project.cmd`
- `coordination\bin\coord-init-project.cmd`
- `coordination\bin\coord-new-worktree.cmd`
- `coordination\bin\coord-log-thought.cmd`
- `coordination\bin\coord-log-operation.cmd`
- `coordination\bin\coord-log-mod.cmd`
- `coordination\bin\coord-read-stream.cmd`
- `coordination\bin\coord-context-digest.cmd`
- `coordination\bin\coord-audit-runtime.cmd`
- `coordination\bin\coord-release-worktree.cmd`

Preferred lifecycle:

1. `coord-install-package` when injecting this package into another repository
2. `coord-init-project`
3. `coord-audit-runtime`
4. `coord-claim-task`
5. send the generated `SESSIONS/active/<session-id>/AI_PROMPT.md`
6. `coord-log-thought`, `coord-log-operation`, `coord-log-mod` during work
7. `coord-read-stream` during active collaboration
8. `coord-handoff-task` or `coord-reopen-task` when ownership changes or work resumes
9. commit or otherwise clean the worktree, then run `coord-close-task` which appends a digest node automatically
10. `coord-archive-project` when the project runtime is retired
