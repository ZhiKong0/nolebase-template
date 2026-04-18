# AI Entry

If you are an AI working inside this repository, start from this file.

This applies before repository-scoped answers, analysis, reviews, and code changes, not only before edits.

## Required Read Chain

1. Read `coordination/runtime/ACTIVE_CONTEXT.md`.
2. Read the preferred project's `AI_START.md`.
3. Follow that project's `AI_START.md` exactly. It is the authoritative project work packet.
4. If the operator assigned you a session prompt under `SESSIONS/active/<session-id>/AI_PROMPT.md`, read it when `AI_START.md` tells you to.
5. Only then inspect repository business files inside your assigned worktree.

## Hard Rules

- Do not guess the active project. Use `ACTIVE_CONTEXT.md`.
- Do not duplicate project-level read order outside `AI_START.md`.
- Do not scan the repository before the indexed read chain unlocks business code.
- Do not assume chat history is shared with other AIs.
- Do not edit another active lock scope directly.
- If your workflow starts from the shell, you may first run `coordination\bin\coord-detect-context.cmd` and follow its `nextReadChain`.
- During active collaboration, prefer `coordination\bin\coord-read-stream.cmd -ProjectId <project-id> -AgentId <agent-id> -OtherAgentsOnly -IncludeSessionHeads` so you only read fresh cross-agent deltas plus the compact active session heads.
- Log intent and decisions through `coordination\bin\coord-log-thought.cmd`.
- Log tool calls and file actions through `coordination\bin\coord-log-operation.cmd`.
- Log completed modifications through `coordination\bin\coord-log-mod.cmd`.
- Poll shared low-latency changes through `coordination\bin\coord-read-stream.cmd`.

## If Runtime Is Missing

- If `coordination/runtime/ACTIVE_CONTEXT.md` does not exist yet, the operator should initialize the runtime with `coordination\bin\coord-init-project.cmd`.
- If no active session exists for you yet, the operator should create one with `coordination\bin\coord-claim-task.cmd` or `coordination\bin\coord-reopen-task.cmd`.
