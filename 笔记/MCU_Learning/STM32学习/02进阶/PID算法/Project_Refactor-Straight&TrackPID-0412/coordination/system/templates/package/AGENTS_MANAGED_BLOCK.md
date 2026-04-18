<!-- coordination-managed:start -->
## Coordination Package

First reads for any AI:

1. `coordination/AI_ENTRY.md`
2. `coordination/runtime/ACTIVE_CONTEXT.md`
3. If `coordination/runtime/projects/` contains a project runtime, read the preferred project's `AI_START.md`
4. Then follow that project's `AI_START.md` exactly
7. If you are assigned a session prompt under `coordination/runtime/projects/<project-id>/SESSIONS/active/<session-id>/AI_PROMPT.md`, read that before business code

Working contract:

- Shared progress lives in `coordination/`, not only in chat memory.
- Parallel coding must use separate `git worktree` checkouts.
- Claim or reopen tasks with `coordination/bin/coord-claim-task.cmd` or `coordination/bin/coord-reopen-task.cmd`.
- Use `coordination/bin/coord-handoff-task.cmd` for baton changes between active AIs.
- Log `THOUGHT` with `coordination/bin/coord-log-thought.cmd`.
- Log `EXEC` with `coordination/bin/coord-log-operation.cmd`.
- Log `MOD` with `coordination/bin/coord-log-mod.cmd`.
- Use `coordination/bin/coord-read-stream.cmd` for low-latency shared updates.
- Do not directly edit another active lock scope.
<!-- coordination-managed:end -->
