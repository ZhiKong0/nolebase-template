# L0_STREAM

Purpose: real-time low-latency triad stream for all active AIs.

## Read Rule

- Poll `NEWEST_ENTRY.json` and `LATEST_TAIL.json` for incremental updates.
- Read `SESSION_HEADS.json` first when you need a compact view of what each active AI is currently doing.
- Do not use `STREAM_LOG/` as long-term history. It only keeps the latest `25` entries.
- If your read pointer falls behind the earliest tail entry, stop and jump to `L1_ACTIVE` or `L2_RECENT`.

## Files

- `NEWEST_ENTRY.json`: newest triad pointer
- `LATEST_TAIL.json`: latest bounded stream tail
- `SESSION_HEADS.json`: compact per-session head state with the latest THOUGHT / EXEC / MOD previews
- `READ_POINTERS/`: per-agent read pointer files
