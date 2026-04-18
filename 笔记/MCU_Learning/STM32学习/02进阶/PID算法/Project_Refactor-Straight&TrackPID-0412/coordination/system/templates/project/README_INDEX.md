---
file_type: readme_index
project_id: {{PROJECT_ID}}
write_mode: script_managed
read_phase: cold_start
last_updated: {{NOW}}
---
# {{PROJECT_NAME}} Read Index

This file is entered from `AI_START.md` only when deeper project structure, boundaries, or recent-history detail is needed.

## Expansion Chain

Read in this order and do not skip ahead:

1. `ARCHITECTURE_TREE.json`
2. `ARCHITECTURE_INDEX.md`
3. `BOUNDARIES/PATH_POLICY.md`
4. `context/BOARD.md`, `context/HANDOFF.md`, and `context/DECISIONS.md` when shared queue, baton, or design context matters
5. `CHANGELOG_TIER/L2_RECENT/RECENT_INDEX.json` only if recent context is still needed
6. `CHANGELOG_TIER/L0_BASE/BASELINE_STATE.json` only if this baseline is new to you
7. only then the business directories required by the task

## Hard Rules

- `AI_START.md` remains the authoritative minimal start packet.
- Do not recursively scan the repository before `AI_START.md` and the expansion chain above unlock business code.
- Do not load `CHANGELOG_TIER/L4_ARCHIVE/` during normal execution.
- Use `L3_DIGEST/latest_summary.md` as the default whole-context shortcut.
- After the digest head is known, later task resumes should read `LIVE_STATE_BOARD.md`, `L1_ACTIVE`, and your `SESSION_DELTA.json` first.

## One-Read Summary

- `ARCHITECTURE_TREE.json` -> machine-readable topology, dependencies, and path zones
- `ARCHITECTURE_INDEX.md` -> human-readable responsibilities and dependency notes
- `BOUNDARIES/PATH_POLICY.md` -> read/write boundaries and patch-only paths
- `CHANGELOG_TIER/L2_RECENT` -> recent finished sessions when the digest head is not enough
- `CHANGELOG_TIER/L3_DIGEST` -> progressive whole-context summary chain for every new task
- `CHANGELOG_TIER/L4_ARCHIVE` -> audit and rollback only; blocked by default
- `context/`, `tasks/`, `locks/` -> shared working memory and current contracts

## Fast Resume Rule

If you already loaded the baseline for this project and the `baselineVersion` is unchanged, your resume path is:

1. `context/LIVE_STATE_BOARD.md`
2. `CHANGELOG_TIER/L3_DIGEST/latest_summary.md`
3. `CHANGELOG_TIER/L1_ACTIVE/ACTIVE_SESSIONS.json`
4. your own `SESSIONS/active/<session-id>/SESSION_DELTA.json`
5. `STREAM_LOG/NEWEST_ENTRY.json`
6. your task file
7. your lock file
