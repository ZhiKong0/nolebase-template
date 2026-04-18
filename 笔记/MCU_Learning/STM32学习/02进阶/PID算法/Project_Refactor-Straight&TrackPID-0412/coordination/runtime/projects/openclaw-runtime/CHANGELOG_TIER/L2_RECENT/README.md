# L2_RECENT

Purpose: recent completed sessions that may still affect current work.

## Read Rule

- Read only when the digest head and active state are insufficient.
- Default retention: keep the last `20` sessions or `72` hours, whichever is shorter.

## Files

- `RECENT_INDEX.json`: recent session index
- `<date>/<session-id>.json`: per-session recent summaries
