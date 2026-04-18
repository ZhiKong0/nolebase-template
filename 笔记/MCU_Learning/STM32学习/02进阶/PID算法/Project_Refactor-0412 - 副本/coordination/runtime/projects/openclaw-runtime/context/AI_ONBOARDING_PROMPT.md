You are joining the `openclaw-runtime` multi-AI workflow.

If the operator provided `SESSIONS\active\<session-id>\AI_PROMPT.md`, that session prompt takes priority over this generic onboarding file.

Repository root: `E:/OpenClaw/.openclaw/workspace`
Project coordination path: `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime`
Default branch: `main`

Before editing:

1. Read `coordination/AI_ENTRY.md`.
2. Read `coordination/runtime/ACTIVE_CONTEXT.md`.
3. Read `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\AI_START.md`.
4. Follow `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\AI_START.md` exactly. Its minimal chain starts with `context\LIVE_STATE_BOARD.md`, `STREAM_LOG\SESSION_HEADS.json`, `CHANGELOG_TIER\L3_DIGEST\latest_summary.md`, and `CHANGELOG_TIER\L1_ACTIVE\ACTIVE_SESSIONS.json`.
5. Read your assigned task file under `tasks\`.
6. Read your assigned lock file under `locks\`.
7. Read your assigned `SESSIONS\active\<session-id>\SESSION_DELTA.json` or generated `AI_PROMPT.md`.
8. Use `coordination\bin\coord-read-stream.cmd -ProjectId openclaw-runtime -AgentId <agent-id> -OtherAgentsOnly -IncludeSessionHeads` for unread live deltas. Treat `STREAM_LOG\NEWEST_ENTRY.json` and `STREAM_LOG\LATEST_TAIL.json` as summary projections only.
9. Read `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\README_INDEX.md` only if you need deeper topology, boundaries, or recent-history detail.
10. Read `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\ARCHITECTURE_TREE.json` and `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\ARCHITECTURE_INDEX.md` only if deeper system structure matters.
11. Read `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\CHANGELOG_TIER\L2_RECENT\RECENT_INDEX.json` only if recent overlap still matters.
12. Only then inspect the business directories required by the task.

Working contract:

- Shared context lives in indexed files, `context\`, `tasks\`, `locks\`, `SESSIONS\`, and `CHANGELOG_TIER\`, not in chat memory.
- Treat `AI_START.md` as the authoritative project work packet. Do not reinvent project-level read order from scratch.
- Use `coordination\bin\coord-log-thought.cmd` for intent and decision records.
- Before any meaningful read, write, delete, patch, or tool action, record it with `coordination\bin\coord-log-operation.cmd`.
- Use `coordination\bin\coord-log-mod.cmd` for concrete modifications.
- Use `coordination\bin\coord-read-stream.cmd -ProjectId openclaw-runtime -AgentId <agent-id> -OtherAgentsOnly -IncludeSessionHeads` to poll low-latency shared updates instead of replaying raw history.
- Treat `STREAM_LOG\STREAM.jsonl` as the active-runtime source of truth and `NEWEST_ENTRY.json` / `LATEST_TAIL.json` as atomic summaries.
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
