# Status handoff

Use this file to start a fresh assistant session. Source and Git state were rechecked on
**2026-09-15** at `HEAD d00e244`. This was a documentation refresh, not a fresh C++ build/test run.
Verify before trusting it with the commands at the end.

Long-range scope and gates: `docs/project-plan-v4.md`

Exact active sequence: `TODO.md`

Plain-language progress, diagrams and worked examples: `docs/project-progress-guide.md`

Current proposed decision: `docs/decisions/0014-event-loop-causality-and-decision-authority.md`

## Working agreement

Fuaad is learning C++ by building the core himself. Explain the domain and C++ mechanism, ask
concrete design questions, and let him attempt learning-critical implementation before supplying it.
Perform requested bookkeeping, tests, CI, documentation and mechanical edits directly. Never commit
or push unless he explicitly asks.

## Verified build state

The previously recorded full build and CTest result is **303 of 303 tests on 2026-09-09**;
the focused BookHealth/OrderBook migration recorded 31 passing tests. These are historical results,
not a fresh claim for this documentation refresh. The mandatory joined-capture fixture is committed
and synthetic; larger private real-capture tests provide additional, optional corpus evidence.

The trust/shape interface changes, glossary and proposed ADR 0014 formerly listed here as
uncommitted were committed in `6b5c0ab`. `d00e244` also clears the prior failure reason when restarting
synchronization from a corrupted state. Neither change completes TODO item 1 or item 2.

At the start of this refresh there were no tracked source changes. Local untracked Claude
commands/skills were present and were preserved. Inspect `git status --short` for current changes;
do not discard local work based on an old handoff inventory.

## Implemented foundation

### Data and capture

- Bitstamp `live_orders` plus `live_trades` is the current capture path. The older Coinbase script
  is a legacy probe; its current venue compatibility was not verified in this refresh.
- Snapshot-backed joined order/trade captures carry a shared local ordinal and immutable raw frames.
- Snapshot parsing, exact integer conversion, chain validation, capture manifests and checkpoint
  comparison are implemented.
- A segment seed must overlap the captured stream; capture refuses a snapshot older than the first
  buffered order event.

### Replay and reconstruction

- `segment_loader.cpp` uses one `decodeCapturedOrder` call per order frame.
- `te::bitstamp::Replay` merges orders and trades deterministically by venue timestamp with the
  order-wins exact-tie rule centralized in `MergeCursor`.
- `TradeReconciler` uses fill credits keyed by `(orderId, venueTimestampMicros)` and corrects only
  uncovered fill quantity, avoiding double subtraction.
- Previously recorded joined real-corpus replay matched the independent checkpoint with
  **0 of 4,533 level residuals**. This documentation refresh did not rerun that corpus test.
- `OrderBook::digest()` and applied-event digests are deterministic across repeated runs.

### Reference order book

- `PriceLevel` owns FIFO list nodes and exact cached aggregate quantity.
- `OrderBook` owns sorted bid/ask maps and an ID-to-stable-iterator locator index.
- Add, quantity change, price-moving change and remove preserve failure atomicity for ordinary
  rejected operations.
- The book is move-only because copied locators would point into the original lists.
- `validateStructure()` checks both directions between levels and index, exact node identity and
  aggregate quantity. Debug builds invoke it after every successful `apply()`.
- `marketShape()` describes visible prices only; it does not grant permission to trade.

### Trust and terminology

- `BookHealth` tracks `unseeded`, `synchronizing`, `valid`, `corrupted` and `fatal_failure`, with
  reason-coded synchronization failures.
- `BookHealth::isTrusted()` reports stream/snapshot trust.
- `MarketShape` reports visible top-of-book shape.
- `Decision readiness` is the future combination of book trust, market shape and operational state.
- `CONTEXT.md` is the canonical short glossary for those distinctions.

## Current task: finish proposed ADR 0014

Do not implement Strategy, Portfolio, DecisionGate, RiskGate, ExecutionVenue or the engine loop until
the causality ADR is accepted.

**What's settled and what's still open lives in `TODO.md` item 1 — read it there.** It used to be
copied here too; two paraphrases of a list that changes every time a sub-question resolves is exactly
the kind of drift this file exists to prevent, so this is the one copy now. Proposed
[ADR 0014](../decisions/0014-event-loop-causality-and-decision-authority.md) has the full reasoning
behind each settled point.

The next teaching question should continue with **minimal Stage 5 operational states** (`TODO.md`
item 1's first open sub-question), not jump into class implementation.

## Stage 5 versus Stage 9 scope

Stage 5 needs real code for the path its gates exercise:

- `NoopStrategy` for complete observation and conservation;
- a scripted strategy that emits an intention;
- a real decision gate over trust, shape and minimal operational state;
- a deterministic simulated venue;
- small genuine admission checks (maximum quantity/notional and resulting absolute position);
- exact fill, fee and portfolio accounting;
- named counters and repeated-run hashes.

Do not add a `RiskGate` that simply returns true. Either a rule is implemented and tested or it stays
explicitly deferred.

Stage 9 owns advanced live controls: operator pause/resume, shutdown orchestration, order-rate and
drawdown limits, production kill switch, heartbeat/disconnect recovery, durable live audit storage,
runbooks and the external paper adapter.

## Known limitations not to overclaim

- The reconciler correction path is exercised by the committed synthetic golden fixture but has
  never fired on observed Bitstamp data: 637 of 637 resting-order trades were already represented by
  `live_orders` fill quantities.
- Three adjustments in the legacy order-only golden replay remain unexplained because that historical
  capture has no trade stream. Do not describe them as proven silent fills.
- v3 tape is a derived accelerator, but replay does not yet consume it. Classifier warm-up and source-lineage
  equivalence remain open until Stage 8 creates a measured need.
- Full structural validation is intentionally absent from the release per-event hot path. Cheap
  always-on local checks are specified but not yet implemented as a complete production policy.
- The review found capture-admission/validator mismatches, unchecked aggregate arithmetic and
  incomplete allocation rollback. The required repairs are tracked in TODO item 7; "implemented
  foundation" does not mean every failure path has been proved safe.
- No Strategy, Portfolio, DecisionGate, ExecutionVenue, complete risk system or engine loop exists
  yet; their headers remain placeholders.

## Commands to reverify

```bash
git status --short
git log --oneline -5
cmake --build build --clean-first -j
ctest --test-dir build --output-on-failure
git diff --check
```

Use `--clean-first` when the result will be cited: this project previously produced a misleading
green incremental build from stale objects.
