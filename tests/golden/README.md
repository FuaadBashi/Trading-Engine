# Golden tests

Recorded input plus expected output, checked into git. These are the tests that catch
regressions you did not think to unit test.

Planned:

- `capture_roundtrip` — 1000 raw JSON lines in, binary records out, read back, byte identical.
- `book_vs_level2` — replay a capture through `OrderBook`, assert top 5 levels match the
  venue's own `level2` snapshots at every snapshot point. Any mismatch is your bug.
- `noop_pnl_zero` — full capture through `NoopStrategy`, PnL and position exactly zero.

Keep fixtures small. A few thousand messages is enough; the full capture belongs in `data/`.
