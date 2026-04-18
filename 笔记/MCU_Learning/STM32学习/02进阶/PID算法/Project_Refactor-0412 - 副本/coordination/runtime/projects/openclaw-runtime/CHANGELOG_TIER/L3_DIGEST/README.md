# L3_DIGEST

Purpose: rolling digest chain for progressive whole-context recovery.

## Read Rule

- New AIs and new tasks should read `latest_summary.md` before `L1_ACTIVE`.
- Use individual `digest_<session-id>.md` files only if the latest summary is insufficient.
- Do not rewrite existing digest nodes.

## Files

- `latest_summary.md`: rolling digest head for fast cold start
- `DIGEST_INDEX.json`: digest chain index
- `digest_<session-id>.md`: immutable per-session digest node
