# L0_BASE

Purpose: immutable structural baseline for architecture and project coordination state.

## Read Rule

- Load only when architectural baseline changed or a structural audit is required.
- Skip during normal incremental task resumes.

## Files

- `BASELINE_STATE.json`: baseline snapshot and baseline version
