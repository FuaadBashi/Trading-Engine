# Status handoff

Use this file to start a fresh assistant session. Source and Git state were rechecked on
**2026-09-16** at `HEAD aef6fc3` (local only — one commit ahead of `origin/main`, not yet pushed).
Verify before trusting it with the commands at the end.

Long-range scope and gates: `docs/project-plan-v4.md`

Exact active sequence: `TODO.md`

The checklist was replaced on 15 September 2026 from the user-supplied `new-todo-list.pdf`.
`TODO.pdf` is the unchanged supplied PDF; `TODO.md` is its Markdown transcription. Its commands,
commit tasks, questions and ADR-update task were imported as list content, not executed. This
replacement supersedes the earlier guide's proposed sequence. Existing build evidence below is
preserved; the checklist import did not rerun tests.

Plain-language progress, diagrams and worked examples: `docs/project-progress-guide.md`

PDF export: `output/pdf/trading-engine-progress-guide.pdf`

**ADR 0014 is accepted (15 September 2026).** All eight causality questions are answered as D1-D8:
`docs/decisions/0014-event-loop-causality-and-decision-authority.md`. Engine implementation now
waits only on section C's foundation repairs, not on the ADR.

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

Freshly verified on **2026-09-15**: a clean out-of-tree configure and build passes **304 of 304
tests**. Two cautions from that run:

- The checked-in `build/` directory no longer configures (`FindThreads only works if either C or CXX
  language is enabled`). The source is fine; the cache is stale. Configure a fresh build directory
  rather than trusting `build/`. Still true as of 2026-09-16 — not yet repaired.
- The corpus tests depend on gitignored capture data that macOS had evicted to iCloud
  (`ls -lO` showed `dataless`; reads returned empty). `brctl download` restored it.

Freshly verified on **2026-09-16** (same out-of-tree build directory, not the stale checked-in
`build/`): **325 of 325 tests**. The loader-completeness gap noted above is now closed —
`validateCapture()` compares the manifest's declared payload/frame-index size, hash, and
frame/order/trade/control counts against what `loadSegment()` actually read, and
`capture_coordinator.cpp` is now the single enforced path: it checks the load result before use,
validates immediately after, and reads `replay()`'s inputs, the cutoff, and the checkpoint
comparison from the validated capture only. Commit `aef6fc3`. Not yet checked: `chain_valid`
(read into the manifest but not compared) and the recorder's `status` field (not read at all) —
see TODO section C for the open note.

The trust/shape interface changes, glossary and ADR 0014 (proposed at the time) formerly listed here
as uncommitted were committed in `6b5c0ab`. `d00e244` also clears the prior failure reason when
restarting synchronization from a corrupted state. ADR 0014 is now accepted (above); Portfolio
implementation has not started.

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
- `validateCapture()` checks a loaded capture against its manifest's declared payload/frame-index
  size, hash, and frame/order/trade/control counts before `capture_coordinator.cpp` will replay it;
  a mismatch returns a named `ValidationError` instead of silently replaying incomplete data.

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

## Current sequence: follow the replacement TODO

Read **Do these first** and sections A-D in `TODO.md`; do not recreate the list here. Section B is
now closed: all eight ADR 0014 questions are answered and recorded as D1-D8, reviewed together and
accepted on 15 September 2026. Do not restart the operational-state discussion (D1) or any of D2-D8
without first reading the ADR — the reasoning behind each is written there, not just the conclusion.

Engine implementation now waits only on section C's remaining foundation repairs (the two unguarded
aggregations, the allocation rollback gap, the skip-vs-fail test, CI Python coverage, the
release-mode structural check). Capture admission is done as of 2026-09-16 (above). The older
progress guide is explanatory background, not authority over the new list.

## Stage 5 versus Stage 9 scope

Stage 5 needs real code for the path its gates exercise:

- `NoopStrategy` for complete observation and conservation;
- a scripted strategy that emits an intention;
- a real decision gate over book trust and market shape only — ADR 0014 D1 defers operational state
  to Stage 9 entirely, so the gate is two inputs, not three, until then;
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
- The review found capture-admission/validator mismatches (fixed 2026-09-16, commit `aef6fc3`),
  unchecked aggregate arithmetic and incomplete allocation rollback. The remaining repairs are
  tracked in TODO section C; "implemented foundation" does not mean every failure path has been
  proved safe.
- The guide's industry recheck adds proposed Stage 5 detail for information availability, outstanding
  exposure, partial-fill/cancel races, a fill journal, run manifests and reproducible scenario traces.
  These remain unimplemented. Information availability specifically is now settled in ADR 0014 D4
  (three named delay policies: `zero`, `fixed` at 80ms, `recorded`); the others are still proposed
  Stage 5 upgrades, not accepted decisions or completed tasks.
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
