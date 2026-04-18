# Start Here

If you copied `coordination/` into a repository, this is the human/operator start file.

If you are an AI, jump to `AI_ENTRY.md` first.

## What This Folder Is

`coordination/` is a file-based shared context package for multi-AI work.

The package enforces three planes:

- control plane: indexed cold-start reads and boundaries
- stream and session plane: `THOUGHT / EXEC / MOD` triad logs plus read pointers
- change plane: tiered delta, digest, and archive layers

## If You Are a New AI

1. Read `AI_ENTRY.md`.
2. Read `runtime/ACTIVE_CONTEXT.md`.
3. Read the preferred project's `AI_START.md`.
4. Follow that project's `AI_START.md` chain exactly.
5. Do not traverse business directories before the indexed read chain unlocks them.

## If You Are the Operator

1. If the target repository does not already contain `coordination/`, run `coordination\bin\coord-install-package.cmd -TargetRepoPath <repo-path>`.
2. Run `coordination\bin\coord-init-project.cmd -ProjectName <name> -RepoPath <repo-path-or-dot>`.
3. Run `coordination\bin\coord-audit-runtime.cmd` and fix structural drift before inviting more AIs.
4. For each active AI task, run `coordination\bin\coord-claim-task.cmd -ProjectId <id> -AgentName <agent> -TaskId <task> -Scope <path>`.
5. Paste the generated `SESSIONS\active\<session-id>\AI_PROMPT.md` into the new AI chat.
6. Require every AI to log `THOUGHT`, `EXEC`, and `MOD` with `coord-log-thought`, `coord-log-operation`, and `coord-log-mod`.
7. Tell active AIs to use `coordination\bin\coord-read-stream.cmd` for low-latency context polling instead of loading raw history.
8. If the baton changes while the task stays active, run `coordination\bin\coord-handoff-task.cmd`.
9. If a closed task needs to resume later, run `coordination\bin\coord-reopen-task.cmd`.
10. When the task pauses or ends, make sure the worktree is committed or clean, then run `coordination\bin\coord-close-task.cmd`; this also appends the `L3_DIGEST` summary node.
11. When the repository no longer needs active coordination runtime, run `coordination\bin\coord-archive-project.cmd`.

## Non-Negotiable Rules

- Never run multiple active coding AIs in the same checkout.
- Never assume another AI saw your chat history.
- Never bypass `AI_ENTRY.md`, `ACTIVE_CONTEXT.md`, or the chosen project's `AI_START.md` on cold start.
- Never write into another active session's locked scope directly.
- If a decision, read, write, delete, modify, or patch matters, log it into the triad before or immediately after the action as required by the command.
