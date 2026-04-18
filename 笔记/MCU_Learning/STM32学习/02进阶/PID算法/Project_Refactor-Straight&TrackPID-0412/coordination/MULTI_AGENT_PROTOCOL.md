# Multi-Agent Protocol

## 1. 📁 Standard Directory Tree Structure

```text
coordination/
  AI_ENTRY.md
  README.md
  START_HERE.md
  ARCHITECTURE.md
  MULTI_AGENT_PROTOCOL.md
  bin/
    coord-install-package.cmd
    coord-init-project.cmd
    coord-claim-task.cmd
    coord-reopen-task.cmd
    coord-handoff-task.cmd
    coord-generate-prompt.cmd
    coord-new-worktree.cmd
    coord-log-operation.cmd
    coord-close-task.cmd
    coord-release-worktree.cmd
    coord-archive-project.cmd
  system/
    templates/
      project/
        README_INDEX.md
        ARCHITECTURE_INDEX.md
        context/
        tasks/
        locks/
        BOUNDARIES/
        CHANGELOG_TIER/
        LOGS/
        SESSIONS/
        STAGING/
  runtime/
    ACTIVE_CONTEXT.md
    ACTIVE_CONTEXT.json
    README.md
    projects/
      <project-id>/
        AI_START.md
        README.md
        README_INDEX.md                # deeper topology and boundary expansion gate
        ARCHITECTURE_INDEX.md          # one-read summary
        ARCHITECTURE_TREE.json         # machine-readable module tree
        project.json
        BOUNDARIES/
          PATH_POLICY.md               # read/write boundaries
        CHANGELOG_TIER/
          L0_BASE/
            README.md
            BASELINE_STATE.json        # baseline snapshot
          L1_ACTIVE/
            README.md
            ACTIVE_SESSIONS.json       # active session index
            <session-id>.json          # live session changelog record
          L2_RECENT/
            README.md
            RECENT_INDEX.json          # recent session index
            <yyyy-mm-dd>/
              <session-id>.json
          L3_DIGEST/
            DIGEST_INDEX.json
            latest_summary.md
            digest_<session-id>.md
          L4_ARCHIVE/
            ARCHIVE_INDEX.json
            <yyyy-mm>/
              <session-id>.json
        STREAM_LOG/
          NEWEST_ENTRY.json
          LATEST_TAIL.json
          READ_POINTERS/
            <agent-id>.json
        LOGS/
          AGENT_LOG_SCHEMA.yaml
          operations/
            README.md
            <yyyy-mm-dd>/
              <session-id>.jsonl
          conflicts/
            README.md
            <yyyy-mm-dd>/
              <session-id>.jsonl
        SESSIONS/
          active/
            <session-id>/
              SESSION_DELTA.json
              THOUGHT.jsonl
              EXEC.jsonl
              MOD.jsonl
          archive/
            <yyyy-mm-dd>/
              <session-id>/
                SESSION_DELTA.json
                THOUGHT.jsonl
                EXEC.jsonl
                MOD.jsonl
        STAGING/
          README.md
          patches/
            README.md
            <session-id>__to__<owner-session-id>__topic.diff
        context/
          LIVE_STATE_BOARD.md
          BOARD.md
          HANDOFF.md
          DECISIONS.md
          AI_ONBOARDING_PROMPT.md
        tasks/
          TASK_TEMPLATE.md
          TASK-001.md
        locks/
          LOCK_TEMPLATE.json
          TASK-001.json
        activity/
          README.md
        state/
          registry.json
    worktrees/
      <project-id>/
        <agent>-<task>/
    archive/
      projects/
        <project-id>/
          <timestamp>/
      worktrees/
        <project-id>/
          <timestamp>/
```

## 2. 📖 Core File Templates

### `ARCHITECTURE_INDEX.md`

```md
---
file_type: architecture_index
project_id: <project-id>
write_mode: script_managed
last_updated: 2026-04-12T04:00:00+08:00
---
# <project-name> Architecture Index

| Module | Responsibility | Depends on |
| --- | --- | --- |
| `README_INDEX.md` | deeper topology and boundary expansion gate | `ARCHITECTURE_TREE.json`, `LIVE_STATE_BOARD.md`, `L3_DIGEST/` |
| `STREAM_LOG/` | low-latency unread tail | `SESSIONS/active/`, `READ_POINTERS/` |
| `CHANGELOG_TIER/L3_DIGEST/` | progressive whole-context summary chain | `L1_ACTIVE`, `L2_RECENT` |
| `tasks/` | task working memory | `locks/`, `SESSIONS/`, `LIVE_STATE_BOARD.md` |
```

### `SESSION_DELTA.json`

```json
{
  "version": 1,
  "sessionId": "SESSION-20260412-040000-codex-a-task-001",
  "projectId": "my-app",
  "taskId": "TASK-001",
  "agentId": "codex-a",
  "status": "active",
  "startedAt": "2026-04-12T04:00:00+08:00",
  "updatedAt": "2026-04-12T04:00:00+08:00",
  "branch": "ai/codex-a/task-001",
  "worktreePath": "D:\\repo\\coordination\\runtime\\worktrees\\my-app\\codex-a-task-001",
  "baselineVersion": "L0-20260412-035500",
  "readState": {
    "baselineLoaded": false,
    "lastReadLevel": "L0_BASE",
    "businessReadUnlocked": false
  },
  "paths": {
    "taskFile": "D:\\repo\\coordination\\runtime\\projects\\my-app\\tasks\\TASK-001.md",
    "lockFile": "D:\\repo\\coordination\\runtime\\projects\\my-app\\locks\\TASK-001.json",
    "l1Entry": "D:\\repo\\coordination\\runtime\\projects\\my-app\\CHANGELOG_TIER\\L1_ACTIVE\\SESSION-20260412-040000-codex-a-task-001.json",
    "thoughtLog": "D:\\repo\\coordination\\runtime\\projects\\my-app\\SESSIONS\\active\\SESSION-...\\THOUGHT.jsonl",
    "execLog": "D:\\repo\\coordination\\runtime\\projects\\my-app\\SESSIONS\\active\\SESSION-...\\EXEC.jsonl",
    "modLog": "D:\\repo\\coordination\\runtime\\projects\\my-app\\SESSIONS\\active\\SESSION-...\\MOD.jsonl",
    "streamNewestEntry": "D:\\repo\\coordination\\runtime\\projects\\my-app\\STREAM_LOG\\NEWEST_ENTRY.json",
    "streamTail": "D:\\repo\\coordination\\runtime\\projects\\my-app\\STREAM_LOG\\LATEST_TAIL.json",
    "readPointerDir": "D:\\repo\\coordination\\runtime\\projects\\my-app\\STREAM_LOG\\READ_POINTERS",
    "liveStateBoard": "D:\\repo\\coordination\\runtime\\projects\\my-app\\context\\LIVE_STATE_BOARD.md",
    "digestLatestSummary": "D:\\repo\\coordination\\runtime\\projects\\my-app\\CHANGELOG_TIER\\L3_DIGEST\\latest_summary.md"
  },
  "triadCounters": {
    "thought": 0,
    "exec": 0,
    "mod": 0
  },
  "delta": {
    "summary": "",
    "touchedFiles": [],
    "pendingWrites": [],
    "conflicts": []
  }
}
```

### `AGENT_LOG_SCHEMA.yaml`

```yaml
version: 1
type: triad_record
required:
  - session_id
  - agent_id
  - timestamp
  - type
  - content
  - related_files
  - status
  - checksum
append_policy:
  mode: append_only_jsonl
  one_record_per_line: true
type_map:
  T: THOUGHT
  E: EXEC
  M: MOD
```

## 3. 🔄 Tiered Change Management Rules

| Level | Definition | Trigger | Read Strategy | Archive Threshold |
| --- | --- | --- | --- | --- |
| `L0_BASE` | immutable baseline | project init or explicit baseline reset | load once per fresh AI or baseline version change | never edited in daily flow |
| `L1_ACTIVE` | live sessions in progress | `coord-new-worktree` or active session updates | read every resume | move out on `coord-release-worktree` |
| `L2_RECENT` | recent completed sessions | session release | read only if task overlaps recent work | keep last `20` sessions or `72` hours |
| `L3_DIGEST` | summary chain | `coord-context-digest` or task close | always read before deeper history | keep all, default load last `8` |
| `L4_ARCHIVE` | historical audit trail | L2 overflow or age-out | blocked in daily execution | permanent until manual cleanup |

## 4. 🤖 AI Standard SOP

1. Cold start:
   Read `AI_ENTRY.md`, then `runtime/ACTIVE_CONTEXT.md`, then the selected project's `AI_START.md`, and follow `AI_START.md` exactly.
2. Package install:
   If the target repo does not contain this package, install it with `coordination\bin\coord-install-package.cmd -TargetRepoPath <repo>`.
3. Task claim:
   The operator should use `coordination\bin\coord-claim-task.cmd` so the task, lock, session, branch, worktree, and `AI_PROMPT.md` are created together.
4. Incremental read:
   If baseline already loaded and unchanged, read `LIVE_STATE_BOARD.md`, `L3_DIGEST/latest_summary.md`, `L1_ACTIVE`, `STREAM_LOG/NEWEST_ENTRY.json`, plus your `SESSION_DELTA.json` or use the generated `AI_PROMPT.md`. Read `README_INDEX.md` only when the task needs deeper topology or boundary detail.
5. Before action:
   Run `coordination\bin\coord-log-thought.cmd` for intent, `coord-log-operation.cmd` for execution, and `coord-log-mod.cmd` after concrete modifications.
6. Execute:
   Work only in your assigned worktree and branch.
7. Shared context sync:
   Update `BOARD.md`, task file, lock file, and `SESSION_DELTA.json` in the same work burst.
8. Handoff or reopen:
   Use `coordination\bin\coord-handoff-task.cmd` for baton changes between active AIs, or `coordination\bin\coord-reopen-task.cmd` to resume closed work later.
9. Handoff or close:
   Update `HANDOFF.md`, make sure the worktree is committed or otherwise removable, then run `coordination\bin\coord-close-task.cmd`; it generates the digest automatically.
10. Project retirement:
   Run `coordination\bin\coord-archive-project.cmd` only when `L1_ACTIVE` is empty.

## 5. ⚠️ Boundaries and Exception Handling

- Conflict handling:
  If a target path falls under another active lock scope, direct write is rejected. Submit a patch under `STAGING/patches/` and append a conflict log.
- Over-read prevention:
  Daily work must not load `L4_ARCHIVE/`. Only audit or rollback tasks may read it.
- Pointer gap handling:
  If the per-agent pointer falls behind the bounded tail window, reload the current tail plus `L3_DIGEST/latest_summary.md` instead of scanning raw history.
- Log corruption recovery:
  If a JSONL log file is damaged, stop appending, create a new date-partitioned log file, and record the recovery event in `LOGS/conflicts/`.
- Idempotent writes:
  Duplicate writes to the same path inside one session are merged in `SESSION_DELTA.json` by de-duplicating `pendingWrites` and `touchedFiles`.
- Metadata header enforcement:
  AI-maintained Markdown files must preserve the YAML header. If missing, restore it before further edits.
- Archive precondition:
  `coord-archive-project` must refuse archive when any active session remains in `L1_ACTIVE` or `ACTIVE_SESSIONS.json`.
