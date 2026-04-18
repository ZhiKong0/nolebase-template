# STREAM_LOG

Purpose: append-only live triad source plus atomic summary files for all active AIs.

## Read Rule

- Prefer `coord-read-stream` for incremental unread deltas.
- Read `SESSION_HEADS.json` first when you need a compact view of what each active AI is currently doing.
- Treat `STREAM.jsonl` as the source of truth for the active runtime.
- Treat `NEWEST_ENTRY.json` and `LATEST_TAIL.json` as lossy summary projections rebuilt from the stream source.
- If your read pointer target no longer exists in the current stream source, stop at the lossy boundary, read the digest head and live board, then rerun `coord-read-stream` with `-AcceptLossyBoundary` only when you intentionally accept resuming from the current stream head.

## Files

- `STREAM.jsonl`: append-only live triad source
- `NEWEST_ENTRY.json`: newest-entry atomic summary
- `LATEST_TAIL.json`: latest bounded summary window
- `SESSION_HEADS.json`: compact per-session head state with the latest THOUGHT / EXEC / MOD previews
- `READ_POINTERS/`: per-agent per-mode read pointer files
