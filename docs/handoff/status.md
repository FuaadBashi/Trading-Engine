# Status handoff

Use this file to start a fresh assistant session. It reflects the repository on **2026-09-09** at
`HEAD 22bc074` plus the uncommitted changes listed below. Verify before trusting it with the commands
at the end.

Long-range scope and gates: `docs/project-plan-v4.md`

Exact active sequence: `TODO.md`

Current proposed decision: `docs/decisions/0014-event-loop-causality-and-decision-authority.md`

## Working agreement

Fuaad is learning C++ by building the core himself. Explain the domain and C++ mechanism, ask
concrete design questions, and let him attempt learning-critical implementation before supplying it.
Perform requested bookkeeping, tests, CI, documentation and mechanical edits directly. Never commit
or push unless he explicitly asks.

## Verified build state

The configured full build and CTest suite pass **303 of 303 tests** on 2026-09-09. The focused
BookHealth/OrderBook migration passes 31 tests. The mandatory joined-capture fixture is committed, so
the principal end-to-end correctness path does not disappear on a fresh checkout. Larger private
capture tests remain additional corpus evidence.

The working tree is intentionally dirty. Current uncommitted work is exactly:

- `BookHealth::isUsable()` renamed to `isTrusted()`, and its test updated;
- `OrderBook::hasUsableBidAsk()` replaced by reason-coded `marketShape()`, returning
  `MarketShape { empty, one_sided, locked, crossed, open }`, and its tests updated to assert each
  case by name instead of one collapsed boolean;
- `CONTEXT.md` (new domain glossary distinguishing book trust, market shape and decision
  readiness) and proposed ADR 0014;
- README/TODO/plan-v4/handoff documentation refreshed to match the above and to point at ADR 0014.

`OrderBook::apply()`'s event-kind dispatch, invalid-side/kind errors, allocation rollback and
debug-build `validateStructure()` are already committed (`HEAD`), not part of this diff.

Do not overwrite or discard these changes.

## Implemented foundation

### Data and capture

- Bitstamp `live_orders` is the primary public L3 source; Coinbase L2 is secondary only.
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
- Joined real-corpus replay matches the independent checkpoint with **0 of 4,533 level residuals**.
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

Already settled:

1. Apply and reconcile a successful market event before strategy observation.
2. Deliver every successfully processed event to observation, even when decisions are blocked.
3. Observation carries no execution authority; strategies eventually return `OrderIntent` values.
4. Book trust, market shape and operational state are independent inputs flowing one way into an
   engine-owned decision gate. They do not mutate one another.
5. Evaluate shape only at a safe checkpoint after all work for one logical input is complete.
6. A strategy-side condition is advisory only; centralized admission and a final venue check enforce
   rejection with named reasons.
7. Debug uses full post-mutation structural sweeps. Release hot paths need cheap local checks; a
   structural failure is fatal `internal_invariant_violation`, not a recoverable market-health issue.
8. Replay operational transitions are scripted deterministic inputs, never ambient wall time.

Still open, and best handled one concrete scenario at a time:

1. Minimal Stage 5 replay operational states and transitions.
2. When an accepted intent enters the simulated outbound-latency queue.
3. First-fill eligibility at arrival time versus only on later market activity.
4. Ordering of exchange, receipt, simulation, control and equal timestamps.
5. Exact acknowledgement/fill/fee/cash/position/average-price/PnL sequence.
6. Observation and decision behaviour during every BookHealth state and transport failure.
7. Stable decision-block, admission-rejection and fatal-run reason enums.
8. Evidence-based escalation from crossed safe checkpoints to lost trust.

The next teaching question should continue with **minimal Stage 5 operational states**, not jump into
class implementation.

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
