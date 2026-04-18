# Coordination Runtime

This folder contains the live coordination state for the current repository.

## Structure

- `ACTIVE_CONTEXT.md`: the AI-first runtime index for the current repository
- `ACTIVE_CONTEXT.json`: machine-readable version of the same index
- `projects/`: one shared context folder per coordinated repository
- `worktrees/`: one isolated checkout per active AI task

Inside each runtime project, the default AI read chain is:

1. `AI_START.md`
2. follow `AI_START.md`

## Rule

`runtime/` is repo-local state, not the reusable package itself.

When you copy `coordination/` into another repository, this is the part you normally reinitialize with `coord-init-project`.
