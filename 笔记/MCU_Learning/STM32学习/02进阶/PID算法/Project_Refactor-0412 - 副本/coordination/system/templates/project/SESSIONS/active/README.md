# Active Sessions

Each active session gets its own folder:

- `SESSIONS/active/<session-id>/SESSION_DELTA.json`
- `SESSIONS/active/<session-id>/THOUGHT.jsonl`
- `SESSIONS/active/<session-id>/EXEC.jsonl`
- `SESSIONS/active/<session-id>/MOD.jsonl`

The session delta is the primary incremental resume file for that session.
The three JSONL files are the session-local full triad record.
