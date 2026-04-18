# Coordination Architecture

This document explains `coordination/` as a portable shared-context system for multiple AIs.

## Design Goal

`coordination/` is not a notes folder.

It is a collaboration package with five enforced behaviors:

- indexed cold starts
- triad logging for `THOUGHT / EXEC / MOD`
- append-only stream sync with read pointers plus atomic summaries
- progressive digest-based context loading
- isolated worktree execution with append-only audit trails

## Top-Level Structure

| Path | Role |
| --- | --- |
| `coordination/AI_ENTRY.md` | single AI-first entrypoint |
| `coordination/README.md` | short package map |
| `coordination/START_HERE.md` | human/operator start file |
| `coordination/MULTI_AGENT_PROTOCOL.md` | full executable protocol |
| `coordination/ARCHITECTURE.md` | architecture reference |
| `coordination/bin/` | portable entrypoints |
| `coordination/tooling/` | implementation |
| `coordination/system/` | reusable assets |
| `coordination/runtime/` | repo-local runtime state |
| `coordination/runtime/ACTIVE_CONTEXT.md` | dynamic active project chooser |
| `coordination/runtime/archive/` | archived runtimes and retired worktree trees |

## Project Runtime Layout

Each coordinated repo gets one folder under `runtime/projects/<project-id>/`.

Archived project runtimes move under `runtime/archive/projects/<project-id>/<timestamp>/`.

Archived worktree trees move under `runtime/archive/worktrees/<project-id>/<timestamp>/`.

### Control Plane

| Path | Purpose |
| --- | --- |
| `AI_START.md` | project-specific AI entrypoint |
| `README_INDEX.md` | deeper topology and boundary expansion gate |
| `ARCHITECTURE_INDEX.md` | one-read architecture summary |
| `ARCHITECTURE_TREE.json` | machine-readable module tree |
| `BOUNDARIES/PATH_POLICY.md` | read/write and patch rules |

### Change Plane

| Path | Purpose |
| --- | --- |
| `CHANGELOG_TIER/L0_BASE/` | baseline state, loaded once |
| `CHANGELOG_TIER/L1_ACTIVE/` | current live sessions |
| `CHANGELOG_TIER/L2_RECENT/` | recent completed sessions |
| `CHANGELOG_TIER/L3_DIGEST/` | digest chain and latest whole-context summary |
| `CHANGELOG_TIER/L4_ARCHIVE/` | long-term cold archive |

### Stream Plane

| Path | Purpose |
| --- | --- |
| `STREAM_LOG/STREAM.jsonl` | append-only project-global triad source of truth |
| `STREAM_LOG/NEWEST_ENTRY.json` | atomic summary of the newest stream entry |
| `STREAM_LOG/LATEST_TAIL.json` | atomic summary window of the latest stream entries |
| `STREAM_LOG/SESSION_HEADS.json` | compact per-session live head state |
| `STREAM_LOG/READ_POINTERS/<agent>.json` | per-agent unread pointer state |

### Session and Audit Plane

| Path | Purpose |
| --- | --- |
| `SESSIONS/active/<session-id>/SESSION_DELTA.json` | incremental resume state |
| `SESSIONS/active/<session-id>/THOUGHT.jsonl` | intent and decision records |
| `SESSIONS/active/<session-id>/EXEC.jsonl` | tool, file, and API execution records |
| `SESSIONS/active/<session-id>/MOD.jsonl` | concrete modifications and patch outcomes |
| `SESSIONS/archive/` | archived session deltas |
| `LOGS/operations/` | append-only JSONL operation logs |
| `LOGS/conflicts/` | conflict evidence |
| `LOGS/CONFLICT_LOG.md` | human-readable conflict ledger |
| `STAGING/patches/` | cross-session patch proposals |

### Shared Context Plane

| Path | Purpose |
| --- | --- |
| `context/` | shared overview and handoff |
| `tasks/` | task-specific working memory |
| `locks/` | ownership and scope contracts |
| `activity/` | optional timeline |
| `state/registry.json` | machine-readable project index |

### State and Transaction Plane

| Path | Purpose |
| --- | --- |
| `state/events/STATE_EVENTS.jsonl` | append-only state transition ledger |
| `state/transactions/active/` | in-flight transaction metadata |
| `state/transactions/history/` | committed or rolled-back transaction metadata |
| `state/internal_locks/` | internal coordination locks and script guardrails |

## Cold Start Chain

1. `coordination/AI_ENTRY.md`
2. `coordination/runtime/ACTIVE_CONTEXT.md`
3. `runtime/projects/<project-id>/AI_START.md`
4. `AI_START.md` then routes into `context/LIVE_STATE_BOARD.md`
5. `AI_START.md` then routes into `STREAM_LOG/SESSION_HEADS.json`
6. `AI_START.md` then routes into `CHANGELOG_TIER/L3_DIGEST/latest_summary.md`
7. `AI_START.md` then routes into `CHANGELOG_TIER/L1_ACTIVE/ACTIVE_SESSIONS.json`
8. `AI_START.md` then prefers `coord-read-stream` for unread live deltas
9. `STREAM_LOG/NEWEST_ENTRY.json` and `STREAM_LOG/LATEST_TAIL.json` are summary projections only
10. assigned task and lock files plus `SESSION_DELTA.json` or generated `AI_PROMPT.md`
11. `README_INDEX.md` only when deeper structure, boundaries, or recent history is needed
12. `ARCHITECTURE_TREE.json` and `ARCHITECTURE_INDEX.md` only when the task needs deeper topology
13. `CHANGELOG_TIER/L2_RECENT/RECENT_INDEX.json` only when overlap exists
14. `CHANGELOG_TIER/L0_BASE/BASELINE_STATE.json` only on first load or baseline change
15. business directories only after the chain is complete

## Incremental Resume Rule

After the baseline is already known, a session resumes from:

1. `context/LIVE_STATE_BOARD.md`
2. `STREAM_LOG/SESSION_HEADS.json`
3. `CHANGELOG_TIER/L3_DIGEST/latest_summary.md`
4. `CHANGELOG_TIER/L1_ACTIVE/ACTIVE_SESSIONS.json`
5. `coord-read-stream` with the current mode-specific pointer
6. its own `SESSIONS/active/<session-id>/SESSION_DELTA.json`
7. its task file
8. its lock file
9. only then changed business files

## Retention Rules

- `L1_ACTIVE`: all active sessions
- `L2_RECENT`: keep the last `20` sessions or `72` hours
- `L3_DIGEST`: keep all digest nodes but only the latest `8` are loaded by default
- `L4_ARCHIVE`: everything older than the L2 window

## Isolation Rule

Each active coding AI gets:

- one branch
- one worktree
- one task file
- one lock file
- one session delta
- three session-local triad streams
- one shared append-only project stream plus atomic summaries

## Lifecycle Commands

- `coord-install-package`: copy the portable package into another repository and optionally seed root `AGENTS.md`
- `coord-init-project`: register a repo and create the runtime scaffold
- `coord-claim-task`: claim a task, create the branch/worktree/session, and generate the exact AI prompt
- `coord-reopen-task`: resume a closed or released task into a new active session and worktree
- `coord-handoff-task`: release one active owner and reopen the same task for the next AI
- `coord-generate-prompt`: refresh or regenerate the exact prompt for a task/session
- `coord-log-thought`: append `THOUGHT` records into the triad and project stream
- `coord-log-operation`: append `EXEC` records into the triad and project stream
- `coord-log-mod`: append `MOD` records into the triad and project stream
- `coord-read-stream`: read only unread tail entries through a per-agent pointer
- `coord-context-digest`: generate one `L3_DIGEST` node from a finished session
- `coord-close-task`: release the worktree, archive the session, generate the digest, and close the task
- `coord-archive-project`: archive the whole runtime after all active sessions are closed
