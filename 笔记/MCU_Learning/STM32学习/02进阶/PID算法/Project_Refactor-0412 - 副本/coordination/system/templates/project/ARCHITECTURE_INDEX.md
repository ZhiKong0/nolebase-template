---
file_type: architecture_index
project_id: {{PROJECT_ID}}
write_mode: script_managed
last_updated: {{NOW}}
---
# {{PROJECT_NAME}} Architecture Index

## Module Summary

| Module | Responsibility | Depends on |
| --- | --- | --- |
| `README_INDEX.md` | deeper topology and boundary expansion gate | `ARCHITECTURE_TREE.json`, `CHANGELOG_TIER/` |
| `ARCHITECTURE_TREE.json` | machine-readable module map and boundaries | `project.json` |
| `BOUNDARIES/` | read/write restrictions and patch rules | `locks/`, `STAGING/` |
| `STREAM_LOG/STREAM.jsonl` | append-only live triad source | `SESSIONS/active/`, `coord-read-stream` |
| `STREAM_LOG/NEWEST_ENTRY.json` | atomic newest-entry summary | `STREAM_LOG/STREAM.jsonl` |
| `STREAM_LOG/LATEST_TAIL.json` | atomic bounded stream window | `STREAM_LOG/STREAM.jsonl` |
| `STREAM_LOG/SESSION_HEADS.json` | compact per-session live heads | `STREAM_LOG/STREAM.jsonl`, `L1_ACTIVE` |
| `CHANGELOG_TIER/L0_BASE` | structural baseline state snapshot | `ARCHITECTURE_TREE.json` |
| `CHANGELOG_TIER/L1_ACTIVE` | active session full records | `SESSIONS/active/`, `THOUGHT/EXEC/MOD` triads |
| `CHANGELOG_TIER/L2_RECENT` | recent completed sessions | `SESSIONS/archive/` |
| `CHANGELOG_TIER/L3_DIGEST` | digest chain and latest whole-context summary | `L1_ACTIVE`, `L2_RECENT`, `context/DECISIONS.md` |
| `CHANGELOG_TIER/L4_ARCHIVE` | cold archive | `L2_RECENT` rollover |
| `SESSIONS/` | per-session delta and triad logs | `tasks/`, `locks/`, `STREAM_LOG/` |
| `LOGS/` | structured operation and conflict evidence | `SESSIONS/`, `STREAM_LOG/` |
| `state/events/STATE_EVENTS.jsonl` | append-only state transition ledger | `claim/init/digest` flows |
| `state/transactions/` | in-flight and historical transaction metadata | `claim/init/digest` flows |
| `context/` | shared overview and handoff | `tasks/`, `locks/` |
| `tasks/` | task-specific working memory | `locks/`, `SESSIONS/` |
| `locks/` | ownership and scope boundaries | `tasks/`, `STAGING/patches/` |
| `runtime/worktrees/` | isolated code-editing checkouts | `git worktree` |

## Dependency Rules

- `context/` is shared and human-readable.
- `tasks/` and `locks/` are the task contract.
- `SESSIONS/active/<session>/SESSION_DELTA.json` is the incremental resume state.
- `SESSIONS/active/<session>/THOUGHT.jsonl`, `EXEC.jsonl`, and `MOD.jsonl` are the session-local full triad logs.
- `STREAM_LOG/STREAM.jsonl` is the live source of truth.
- `NEWEST_ENTRY.json`, `LATEST_TAIL.json`, and `SESSION_HEADS.json` are atomic summary files rebuilt from the stream source.
- `LOGS/operations/` is the EXEC evidence log and must be written before read/write/delete/patch actions.
- `STAGING/patches/` is the only allowed route for proposing changes inside another active session's scope.

## Baseline and Delta Model

- Structural baseline lives in `CHANGELOG_TIER/L0_BASE/`.
- Live append source lives in `STREAM_LOG/STREAM.jsonl`.
- Live summary files live in `STREAM_LOG/NEWEST_ENTRY.json`, `STREAM_LOG/LATEST_TAIL.json`, and `STREAM_LOG/SESSION_HEADS.json`.
- Active session records live in `CHANGELOG_TIER/L1_ACTIVE/` plus `SESSIONS/active/`.
- Recent history lives in `CHANGELOG_TIER/L2_RECENT/`.
- Progressive summary chain lives in `CHANGELOG_TIER/L3_DIGEST/`.
- Cold history lives in `CHANGELOG_TIER/L4_ARCHIVE/`.
