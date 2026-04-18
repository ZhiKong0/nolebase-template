# L2_RECENT

Purpose: recent completed sessions that may still affect current work.

## Read Rule

- Read only when the digest head and active state are insufficient.
- Default retention: keep the last `{{RECENT_RETENTION_COUNT}}` sessions or `{{RECENT_RETENTION_HOURS}}` hours, whichever is shorter.

## Files

- `RECENT_INDEX.json`: recent session index
- `<date>/<session-id>.json`: per-session recent summaries
