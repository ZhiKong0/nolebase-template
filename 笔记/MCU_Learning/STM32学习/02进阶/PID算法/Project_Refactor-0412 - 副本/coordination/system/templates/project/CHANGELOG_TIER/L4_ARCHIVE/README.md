# L4_ARCHIVE

Purpose: cold archive for raw historical session records.

## Read Rule

- Do not load during normal execution.
- Load only for audit, rollback, or forensic investigation.

## Files

- `ARCHIVE_INDEX.json`: archived session index
- `<month>/<session-id>.json`: archived summaries
