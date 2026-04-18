# L1_ACTIVE

Purpose: active live deltas for in-flight sessions.

## Read Rule

- Highest priority after `LIVE_STATE_BOARD.md` and the digest head.
- Read on every task resume.

## Files

- `ACTIVE_SESSIONS.json`: index of current live sessions
- `<session-id>.json`: current live changelog record for one session
- session-local `THOUGHT/EXEC/MOD` logs remain under `SESSIONS/active/<session-id>/`
