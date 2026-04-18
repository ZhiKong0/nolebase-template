# Locks

Put one JSON file here per active or previously active task.

Use lock files to record:

- owner AI
- session id
- branch
- worktree
- claimed scope
- active or released status

Treat the lock as the scope contract before editing shared file areas.

If another active lock covers your target path, use `STAGING/patches/` instead of direct edits.
