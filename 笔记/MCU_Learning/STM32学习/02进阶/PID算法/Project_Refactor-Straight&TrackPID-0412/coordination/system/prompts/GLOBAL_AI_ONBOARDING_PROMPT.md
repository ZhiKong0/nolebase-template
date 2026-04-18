# Global AI Onboarding Prompt

Use this when you are starting a new AI chat and do not yet have a project-specific prompt in front of you.

If the operator already generated `SESSIONS\active\<session-id>\AI_PROMPT.md`, use that session-specific prompt instead of this generic one.

```text
You are joining a repository that uses a portable `coordination/` package for multi-AI coding.

Project name: <PROJECT_NAME>
Repository root: <REPO_ROOT>
Project coordination path: <PROJECT_COORDINATION_PATH>
Task id: <TASK_ID>
Branch: <BRANCH_NAME>
Worktree path: <WORKTREE_PATH>
Session id: <SESSION_ID>

Before editing anything:
1. Read <REPO_ROOT>\coordination\START_HERE.md
2. Read <REPO_ROOT>\coordination\AI_ENTRY.md
3. Read <REPO_ROOT>\coordination\runtime\ACTIVE_CONTEXT.md
4. Read <PROJECT_COORDINATION_PATH>\AI_START.md
5. Follow <PROJECT_COORDINATION_PATH>\AI_START.md exactly. Its minimal chain starts with `LIVE_STATE_BOARD.md`, `L3_DIGEST/latest_summary.md`, `L1_ACTIVE/ACTIVE_SESSIONS.json`, and `STREAM_LOG/NEWEST_ENTRY.json`.
6. Read <PROJECT_COORDINATION_PATH>\tasks\<TASK_ID>.md
7. Read <PROJECT_COORDINATION_PATH>\locks\<TASK_ID>.json
8. Read <PROJECT_COORDINATION_PATH>\SESSIONS\active\<SESSION_ID>\SESSION_DELTA.json or the generated AI prompt if present
9. Read <PROJECT_COORDINATION_PATH>\README_INDEX.md only if you need deeper topology, boundaries, or recent-history detail
10. Read <PROJECT_COORDINATION_PATH>\ARCHITECTURE_TREE.json and <PROJECT_COORDINATION_PATH>\ARCHITECTURE_INDEX.md only if deeper system structure matters
11. Read <PROJECT_COORDINATION_PATH>\CHANGELOG_TIER\L2_RECENT\RECENT_INDEX.json only if overlap with recent work matters
12. Read <PROJECT_COORDINATION_PATH>\CHANGELOG_TIER\L0_BASE\BASELINE_STATE.json only if this is your first load or the baseline changed

Working contract:
- Shared context lives in files, not chat memory.
- Treat `AI_START.md` as the authoritative project work packet.
- Work only in your assigned branch and worktree.
- Log intent and decisions with `coordination\bin\coord-log-thought.cmd`.
- Log tool calls and file actions with `coordination\bin\coord-log-operation.cmd`.
- Log completed modifications with `coordination\bin\coord-log-mod.cmd`.
- Poll incremental updates with `coordination\bin\coord-read-stream.cmd`.
- Do not edit another AI's active scope directly.
- If a target path is owned by another active lock, submit a patch under `STAGING\patches\`.
- Update `BOARD.md`, your task file, your lock file, and your `SESSION_DELTA.json` as work changes.
- Before stopping, update `HANDOFF.md`.
- Before the operator closes the task, make sure the worktree is committed or clean enough to remove safely.
```
