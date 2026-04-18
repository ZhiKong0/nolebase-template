---
file_type: ai_start
project_id: openclaw-runtime
write_mode: script_managed
last_updated: 2026-04-12T11:06:12.0696989+08:00
---
# AI Start

## Project

- Project id: `openclaw-runtime`
- Project name: `openclaw-runtime`
- Repository root: `E:/OpenClaw/.openclaw/workspace`
- Coordination root: `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime`
- Worktree root: `E:\OpenClaw\coordination\runtime\worktrees\openclaw-runtime`
- Default branch: `main`
- Baseline version: `L0-20260412-110611`

## Minimal Read Chain

1. `context/LIVE_STATE_BOARD.md`
2. `CHANGELOG_TIER/L3_DIGEST/latest_summary.md`
3. `CHANGELOG_TIER/L1_ACTIVE/ACTIVE_SESSIONS.json`
4. `STREAM_LOG/NEWEST_ENTRY.json`
5. if assigned, read your generated `SESSIONS/active/<session-id>/AI_PROMPT.md`; otherwise read your task file, lock file, and `SESSIONS/active/<session-id>/SESSION_DELTA.json`
6. `README_INDEX.md` only if you need deeper topology, boundary, or recent-history detail
7. only then the business files inside the repository worktree

## Expand Only If Needed

- Read `ARCHITECTURE_TREE.json` and `ARCHITECTURE_INDEX.md` when system layout, dependencies, or ownership boundaries matter.
- Read `CHANGELOG_TIER/L2_RECENT/RECENT_INDEX.json` only if the task overlaps recent finished work.
- Read `CHANGELOG_TIER/L0_BASE/BASELINE_STATE.json` only if baseline version changes or this project is new to you.
- Read `context/BOARD.md`, `context/HANDOFF.md`, and `context/DECISIONS.md` when queue state, baton changes, or durable design decisions matter.

## Current State

- Active sessions: 0
- Active tasks: 0
- Recent sessions: 0
- Digest nodes: 0
- Archived sessions: 0

## Active Sessions

- No active sessions. Use `coordination\bin\coord-claim-task.cmd` to start the next task session.

## Shared Context Files

- Read gate: `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\README_INDEX.md`
- Live state board: `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\context\LIVE_STATE_BOARD.md`
- Latest digest: `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\CHANGELOG_TIER\L3_DIGEST\latest_summary.md`
- Newest stream entry: `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\STREAM_LOG\NEWEST_ENTRY.json`
- Board: `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\context\BOARD.md`
- Handoff: `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\context\HANDOFF.md`
- Decisions: `E:\OpenClaw\coordination\runtime\projects\openclaw-runtime\context\DECISIONS.md`

## Session Prompt Files

- No generated session prompts are registered yet.

## Logging Commands

- THOUGHT: `coordination\bin\coord-log-thought.cmd -ProjectId openclaw-runtime -SessionId <session-id> -AgentId <agent-id> -Content "<intent or decision>"`
- EXEC: `coordination\bin\coord-log-operation.cmd -ProjectId openclaw-runtime -SessionId <session-id> -AgentId <agent-id> -ActionType <type> -TargetPath <path> -DeltaSummary "<why>"`
- MOD: `coordination\bin\coord-log-mod.cmd -ProjectId openclaw-runtime -SessionId <session-id> -AgentId <agent-id> -Content "<what changed>" -RelatedFiles <path>`
- STREAM: `coordination\bin\coord-read-stream.cmd -ProjectId openclaw-runtime -AgentId <agent-id>`

## Rule

- Do not inspect business code before the indexed read chain is complete.
- Work only in your assigned worktree and branch.
- Treat this file as the project-level work packet; outer files should route you here, not duplicate deeper read order.
- If you were given a session prompt, read it after steps 1-4 and before business code.
- Default collaboration mode is progressive: read the digest head and active tail first, then widen only when the task truly needs it.