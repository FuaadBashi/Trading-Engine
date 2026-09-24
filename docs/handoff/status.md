# Status handoff

Start a fresh session from here. [TODO.md](../../TODO.md) owns task status; this file only says
what is true now and what to be careful of.

## Where things are

- **Stage 5, D2 in progress.** `Portfolio::applyFill` handles opening a long (1 of 16 spec tests
  enabled). Every other transition returns `FillError::unsupported_transition`, which is
  temporary. `unrealizedAt` fails loudly until implemented.
- ADR 0014 D5 holds the twelve accounting rules the tests encode. Don't reopen them.
- Capture, merge, reconciliation and the reference book are done: joined replay reproduces the
  venue checkpoint exactly (0 of 4,533 levels differ).
- Not built: strategy, simulated venue, risk, engine loop, replay executable, live path.
  Placeholder files for these were removed; create each file when its code is written.

## Limits the next code must respect

1. **Price x quantity** needs a 128-bit intermediate and checked rescaling (ADR 0004, D5 rule 11).
2. **Admission:** C++ validation omits `chain_valid` and `status`; Python/C++ agreement is TODO C2.
3. **Tape:** the writer doesn't validate ordering or window consistency (C3). Raw/tape
   equivalence is unproven, so the tape can't yet replace raw capture.
4. **L3 evidence:** the book digest covers aggregate levels, not order identity or priority.
5. **Allocation:** heap exhaustion is fatal and nothing may catch it (ADR 0015); catching it
   leaves a ghost level.
6. **Latency:** the capture's local timestamps are *application* receive time (stamped when the
   websocket library hands Python the frame), not wire arrival, and receipt-minus-venue time also
   includes unknown clock offset. Neither is network latency. The v3 tape drops receive time
   entirely; that must change before D5 can model feed latency.

## Working agreement

Fuaad writes learning-critical code. The assistant writes all tests from agreed examples, plus
docs, reviews, bug fixes and mechanical changes. No commit, push or discard without being asked.

## Build

Build in `~/build/TradingEngineProject`, outside iCloud sync. The in-tree `build/` is slow and
rewrites `compile_commands.json` with paths that break the editor.

```bash
cmake --build ~/build/TradingEngineProject -j
ctest --test-dir ~/build/TradingEngineProject --output-on-failure
```
