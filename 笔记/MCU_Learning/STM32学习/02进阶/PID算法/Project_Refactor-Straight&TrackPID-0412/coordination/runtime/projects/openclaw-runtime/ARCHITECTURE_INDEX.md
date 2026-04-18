---
file_type: architecture_index
project_id: openclaw-runtime
write_mode: script_managed
last_updated: 2026-04-12T11:06:11.6617986+08:00
---
# openclaw-runtime Architecture Index

## Module Summary

| Module | Responsibility | Depends on |
| --- | --- | --- |
| `README_INDEX.md` | deeper topology and boundary expansion gate | `ARCHITECTURE_TREE.json`, `CHANGELOG_TIER/` |
| `ARCHITECTURE_TREE.json` | machine-readable module map and boundaries | `project.json` |
| `BOUNDARIES/` | read/write restrictions and patch rules | `locks/`, `STAGING/` |
| `STREAM_LOG/` | low-latency triad tail and read pointers | `SESSIONS/active/`, `LIVE_STATE_BOARD.md` |
| `CHANGELOG_TIER/L0_BASE` | structural baseline state snapshot | `ARCHITECTURE_TREE.json` |
| `CHANGELOG_TIER/L1_ACTIVE` | active session full records | `SESSIONS/active/`, `THOUGHT/EXEC/MOD` triads |
| `CHANGELOG_TIER/L2_RECENT` | recent completed sessions | `SESSIONS/archive/` |
| `CHANGELOG_TIER/L3_DIGEST` | digest chain and latest whole-context summary | `L1_ACTIVE`, `L2_RECENT`, `context/DECISIONS.md` |
| `CHANGELOG_TIER/L4_ARCHIVE` | cold archive | `L2_RECENT` rollover |
| `SESSIONS/` | per-session delta and triad logs | `tasks/`, `locks/`, `STREAM_LOG/` |
| `LOGS/` | structured operation and conflict evidence | `SESSIONS/`, `STREAM_LOG/` |
| `context/` | shared overview and handoff | `tasks/`, `locks/` |
| `tasks/` | task-specific working memory | `locks/`, `SESSIONS/` |
| `locks/` | ownership and scope boundaries | `tasks/`, `STAGING/patches/` |
| `runtime/worktrees/` | isolated code-editing checkouts | `git worktree` |

## Dependency Rules

- `context/` is shared and human-readable.
- `tasks/` and `locks/` are the task contract.
- `SESSIONS/active/<session>/SESSION_DELTA.json` is the incremental resume state.
- `SESSIONS/active/<session>/THOUGHT.jsonl`, `EXEC.jsonl`, and `MOD.jsonl` are the session-local full triad logs.
- `STREAM_LOG/` is the bounded latest tail for low-latency polling.
- `LOGS/operations/` is the EXEC evidence log and must be written before read/write/delete/patch actions.
- `STAGING/patches/` is the only allowed route for proposing changes inside another active session's scope.

## Baseline and Delta Model

- Structural baseline lives in `CHANGELOG_TIER/L0_BASE/`.
- Live tail lives in `STREAM_LOG/`.
- Active session records live in `CHANGELOG_TIER/L1_ACTIVE/` plus `SESSIONS/active/`.
- Recent history lives in `CHANGELOG_TIER/L2_RECENT/`.
- Progressive summary chain lives in `CHANGELOG_TIER/L3_DIGEST/`.
- Cold history lives in `CHANGELOG_TIER/L4_ARCHIVE/`.
