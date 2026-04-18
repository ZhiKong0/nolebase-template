You are joining the `{{PROJECT_NAME}}` multi-AI workflow.

If the operator provided `SESSIONS\active\<session-id>\AI_PROMPT.md`, that session prompt takes priority over this generic onboarding file.

Repository root: `{{REPO_PATH}}`
Project coordination path: `{{COORDINATION_PATH}}`
Default branch: `{{DEFAULT_BRANCH}}`

Before editing:

1. Read `coordination/AI_ENTRY.md`.
2. Read `coordination/runtime/ACTIVE_CONTEXT.md`.
3. Read `{{COORDINATION_PATH}}\AI_START.md`.
4. Follow `{{COORDINATION_PATH}}\AI_START.md` exactly. Its minimal chain starts with `context\LIVE_STATE_BOARD.md`, `CHANGELOG_TIER\L3_DIGEST\latest_summary.md`, `CHANGELOG_TIER\L1_ACTIVE\ACTIVE_SESSIONS.json`, and `STREAM_LOG\NEWEST_ENTRY.json`.
5. Read your assigned task file under `tasks\`.
6. Read your assigned lock file under `locks\`.
7. Read your assigned `SESSIONS\active\<session-id>\SESSION_DELTA.json` or generated `AI_PROMPT.md`.
8. Read `{{COORDINATION_PATH}}\README_INDEX.md` only if you need deeper topology, boundaries, or recent-history detail.
9. Read `{{COORDINATION_PATH}}\ARCHITECTURE_TREE.json` and `{{COORDINATION_PATH}}\ARCHITECTURE_INDEX.md` only if deeper system structure matters.
10. Read `{{COORDINATION_PATH}}\CHANGELOG_TIER\L2_RECENT\RECENT_INDEX.json` only if recent overlap still matters.
11. Only then inspect the business directories required by the task.

Working contract:

- Shared context lives in indexed files, `context\`, `tasks\`, `locks\`, `SESSIONS\`, and `CHANGELOG_TIER\`, not in chat memory.
- Treat `AI_START.md` as the authoritative project work packet. Do not reinvent project-level read order from scratch.
- Use `coordination\bin\coord-log-thought.cmd` for intent and decision records.
- Before any meaningful read, write, delete, patch, or tool action, record it with `coordination\bin\coord-log-operation.cmd`.
- Use `coordination\bin\coord-log-mod.cmd` for concrete modifications.
- Use `coordination\bin\coord-read-stream.cmd` to poll low-latency shared updates instead of replaying raw history.
- Work only in your assigned branch and worktree.
- Do not edit another AI's active scope directly.
- If another active lock covers your target path, submit a patch under `STAGING\patches\` instead.
- Update `context\BOARD.md` when queue, active, blocked, or done state changes.
- Keep your task file current with plan changes, findings, touched files, and verification.
- Update your lock file if scope or ownership changes.
- Update your `SESSIONS\active\<session-id>\SESSION_DELTA.json` as the live incremental state for your session.
- Update `context\HANDOFF.md` before stopping.
- Add important design or workflow decisions to `context\DECISIONS.md`.
- Before the operator closes the task, make sure the worktree is committed or clean enough to remove safely.

If coordination files and repository reality disagree, trust the repository state and record the mismatch immediately in `context\HANDOFF.md` and your session delta.
