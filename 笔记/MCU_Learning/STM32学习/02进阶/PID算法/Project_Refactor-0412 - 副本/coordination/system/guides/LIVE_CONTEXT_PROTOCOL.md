# Live Context Protocol

## Goal

Make `coordination/` behave like a shared live context, even though each AI has its own private chat thread.

## Indexed Read Contract

Cold start order:

1. `AI_ENTRY.md`
2. `runtime/ACTIVE_CONTEXT.md`
3. selected project's `AI_START.md`
4. `AI_START.md` then routes into `LIVE_STATE_BOARD.md`, `STREAM_LOG/SESSION_HEADS.json`, `L3_DIGEST/latest_summary.md`, and `L1_ACTIVE/ACTIVE_SESSIONS.json`
5. `AI_START.md` then prefers `coord-read-stream` for unread live deltas
6. `NEWEST_ENTRY.json` and `LATEST_TAIL.json` are summary projections only
7. assigned task, lock, and `SESSION_DELTA.json` or generated `AI_PROMPT.md`
8. `README_INDEX.md`, `ARCHITECTURE_TREE.json`, and `ARCHITECTURE_INDEX.md` only when deeper structure or boundary detail is required
9. `CHANGELOG_TIER/L2_RECENT/RECENT_INDEX.json` only on overlap
10. `CHANGELOG_TIER/L0_BASE/BASELINE_STATE.json` only on first load or baseline change

## Write Contract

- Log first with `coord-log-operation`
- Update the task file, lock file, and `SESSION_DELTA.json`
- Update `BOARD.md` or `HANDOFF.md` if shared status changed
- Poll `coord-read-stream -OtherAgentsOnly -IncludeSessionHeads` during active multi-AI work so you only read cross-agent deltas plus compact live heads
- Treat `STREAM_LOG/STREAM.jsonl` as the live source of truth and `NEWEST_ENTRY.json` / `LATEST_TAIL.json` as atomic summaries

## Lifecycle Contract

- Install the package into new repositories with `coord-install-package`.
- Claim with `coord-claim-task` so the worktree, session, and `AI_PROMPT.md` stay aligned.
- Resume released work with `coord-reopen-task`.
- Change owners with `coord-handoff-task` instead of editing lock files manually.
- Refresh the exact onboarding text with `coord-generate-prompt` when task metadata changes.
- Close with `coord-close-task` instead of manually releasing the worktree.
- Archive the runtime with `coord-archive-project` only after `L1_ACTIVE` is empty.

## Real-Time Meaning

This system is effectively real-time only when each AI writes the shared files within the same work burst in which reality changed.

## Blocked Actions

- direct edits inside another active lock scope
- daily loading of `L4_ARCHIVE`
- removing YAML metadata headers from AI-maintained Markdown files
