# Trading Engine - active checklist

The single active task list. [Plan v4](docs/project-plan-v4.md) owns long-range scope;
[the guide](docs/project-progress-guide.md) explains the work. Superseded plans and PDF exports
are in [docs/archive](docs/archive/README.md). No deadlines are agreed.

## Start here

**Next: D2, finish Portfolio.** First settle the arithmetic examples and replace the skipped
closing-test scaffold with assertions, then implement one transition at a time. Fuaad writes the
learning-critical code; the assistant writes tests from agreed examples and handles small repairs.

Work order: **D2 → C2/C3 before capture/tape use → D3-D7 → E1 → research/performance extensions.**
C4-C6 are supporting work with the gates below. SPSC is optional as an isolated learning exercise;
it is not a prerequisite for Portfolio and is not integrated before Stage 8.

### Review list: where each of the 30 items belongs

This is a navigation map, not a second checklist. The sections named here own completion.
Review numbers refer to the September 2026 ranked list; order follows dependencies, not just size.

| Review items | Work | Owner below / when |
|---|---|---|
| 1-4, 8 | Test units/scaffold, rounding, checked arithmetic, valid fills, Portfolio/journal | D2, next; only unit correction and honest scaffold status are done |
| 5 | Tape ordering/window checks | C3, before tape use |
| 6 | Allocation-failure behavior | C6, before recovery or bindings |
| 7 | Python/C++ capture admission | C2, before trusted capture runs |
| 9 | Error context | C4, alongside capture hardening |
| 10 | Documentation drift | C8; small factual repairs done, accounting claims still open |
| 11 | Full L3 oracle | E: L3 evidence, before Stage 6/8 claims |
| 12 | Public one-command example | D7 |
| 13 | Availability timestamps | D5 and E: tape equivalence |
| 14-15 | Liquidity rules, risk and order lifecycle | D4 |
| 16 | Simulated venue and complete engine loop | D4-D6 |
| 17 | Feed versus order-entry latency | D5 |
| 18 | Experiment runner and run records | D7 then E1 |
| 19 | Fuzzing and fault tests | E3 |
| 20-21 | Baseline and measured optimization | S8.1-S8.3, S8.6-S8.7 |
| 22 | SPSC queue | S8.5; optional isolated exercise earlier |
| 23 | Build/interface boundaries | C9; test definitions moved, interface work open |
| 24 | Python batch bindings | E4, after E1's CLI route |
| 25 | TWAP and market-making experiment | E5, after the engine and E1 |
| 26 | Optimized book comparison | S8.4 |
| 27 | One networking/protocol exercise | E6, after Stage 5 |
| 28 | Backfill or second venue | E7, optional after the core |
| 29 | Drying-model comparisons | E8, supervisor/data dependent |
| 30 | Advanced infrastructure | E9, optional specialization |

C2 gates trusted capture-to-engine results; C3 gates tape use. In-memory Portfolio work does not
wait on either. Fuaad writes learning-critical code; the assistant writes tests from agreed
examples and must not silently choose open policy.

**No new ADR until the code it governs exists.** Write the simple version, find the edge cases
through tests, then record the decision.

## A. Preserve data and reproducibility

- [x] **Capture recovery/backup:** user reports the backup task complete. Earlier recovery
  checks found no remaining `dataless` files. An independent byte comparison of the backup
  was not performed; do not turn that distinction into a repeated blocker.
- [x] **Broken build directories:** repaired on 16 September. The recorded cause was
  iCloud conflict duplicates. Do not repeat the old deletion instructions.
- [x] **Build outside the synced Desktop tree.** Done 21 September 2026. The canonical build is
  now `~/build/TradingEngineProject`, outside iCloud sync:

  ```bash
  cmake -S . -B ~/build/TradingEngineProject -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build ~/build/TradingEngineProject -j
  ctest --test-dir ~/build/TradingEngineProject --output-on-failure
  ```

  Measured, not assumed: reconfiguring the durable build takes **2.8 seconds**; the same command
  against the in-tree `build/` timed out at **5 minutes**. 327/327 pass there.

  This also fixes editor tooling. `compile_commands.json` is copied from whichever directory you
  build in, so building anywhere transient points clangd at paths that later vanish and every
  `#include` goes red. Build in the durable directory and the file stays valid. The in-tree
  `build/` still exists and still resyncs; prefer deleting it so it cannot silently become the
  source of `compile_commands.json` again.
- [x] **Previously pending work:** present in history at the review baseline; the checkout
  was clean before this documentation update. New changes remain uncommitted until requested.

## B. Design status

- [x] **ADR 0014 D1-D8 accepted, 15 September.** Stage 5 decision inputs are book trust
  and market shape; operational state belongs to Stage 9.
- [x] **ADR 0015 accepted, 16 September:** fatal allocation failure, unsafe object,
  explicitly interim. This does not mean rollback has been implemented.
- [x] **D5's twelve accounting rules selected, 19 September** through D1 below.
  Recorded policy is not implemented accounting; D2 remains open, including the arithmetic clarifications below.
- [ ] **Choose Strategy dispatch when implementing the seam.** Compare runtime and static
  alternatives in ADR 0009. Do not assume templates or `std::function` are inherently faster.

## C. Additional hardening from the 18 September review

### C1. Make sanitizer findings fail CI

- [x] **Done 18 September 2026.** `cmake/sanitizers.cmake` now adds
  `-fno-sanitize-recover=undefined` whenever `TE_SANITIZE` includes `undefined`.

  The gap was measured, not assumed. A signed-overflow probe built under the previous
  configuration printed `runtime error: signed integer overflow` and **exited 0**; with the flag
  it terminates nonzero before reaching its own `printf`. So UBSan was detecting real UB and
  letting the run stay green — detection without enforcement.

  A compile/link flag rather than `UBSAN_OPTIONS=halt_on_error`: the environment variable binds
  only where it is exported, so local sanitized builds would have kept recovering silently.

  `tests/ub_probe.cpp` holds the deliberate UB. It is guarded by `TE_BUILD_UB_PROBE` (OFF by
  default), is not registered with CTest, and is never linked into `te_core` or any app, so no
  intentional UB enters the ordinary suite. A CI step builds it separately per compiler and fails
  if it exits 0, asserting **nonzero** rather than a specific status, since SIGABRT surfaces
  differently across platforms.

  **Scope limit worth remembering:** this covers UBSan's checks, which include *signed* overflow.
  Unsigned wraparound is well-defined in C++, is not UB, and is **not** caught — the explicit
  boundary tests in `trade_reconciler`, `capture_coordinator` and `price_level` remain the only
  guard there.

  **It found a real bug on its first CI run.** `Sha256.EmptyInput` failed under both gcc and
  clang at `sha256.cpp:96:30`: an empty span's `data()` may be `nullptr`, and `memcpy` declares
  `src` nonnull, so passing it was undefined even with a zero length. Harmless on every real
  libc, but the compiler may infer from the nonnull attribute that the pointer cannot be null and
  drop a later check on that basis. That UB predates C1 and UBSan had been reporting it on every
  sanitized run since the SHA-256 work landed — it just scrolled past in a green log. Fixed in
  `a1e1f9c`; digests unchanged, all eight known vectors still match.

  Note this was a *nonnull-attribute* violation, not signed overflow: local runs were clean
  because the probe only exercised the signed-overflow check. Do not read "the local suite
  passed" as "the codebase has no UB."

  Verified: 327/327 under UBSan with enforcement active (before and after the fix), 327/327
  Release with real `gcc-15` (unaffected, the guard skips non-sanitized builds), and CI green on
  `build (gcc)`, `build (clang)` and `release` at `a1e1f9c`. The ASan+UBSan suite could not be
  run locally — `gtest_discover_tests` fails against ASan-instrumented binaries on this macOS
  host, a pre-existing limitation; CI covers that config.

### C2. Align Python and C++ capture admission

- [ ] **Significant; 1-3 days.** Byte/hash/count checks exist. C++ still does not enforce
  `status` or `chain_valid`. Agree on continuity, ordinals, seed coverage and control boundaries
  across `manifest_reader`, `segment_loader`, `capture_validator` and the Python validator.
  **Done when:** shared valid/invalid fixtures receive consistent decisions; interrupted,
  failed and gapped intervals are rejected or explicitly quarantined under a documented policy.
  They must never silently become a trusted continuous segment.

### C3. Validate tape inputs before opening output

- [ ] **Significant; 2-4 hours.** Ordinary replay checks timestamp ordering; the writer does not.
  Check both streams, seed/cutoff order and header/capture/supplied-seed agreement under the API contract.
  **Done when:** timestamps [30, 10] with cutoff 20 and inconsistent metadata are rejected
  before creating output. Do not silently sort a broken stream to conceal a continuity failure.

### C4. Preserve useful errors

- [ ] **Minor now; 4-8 hours.** Keep detailed loader/validator/replay causes plus segment/location
  through `captureCoordinator`; format them at the application boundary.
  **Done when:** a corrupt fixture reports what failed and where without debugging into the loader.

### C5. Refresh the supported toolchain contract

- [ ] **Minor; 2-4 hours plus compatibility work.** Move local/CI Python from EOL 3.9 to a
  supported interpreter and run all Python tests. Keep deliberate archive hashes and dependency pins.
  **Done when:** the documented local and CI environments agree and pass.
  Add a local websocket integration test before treating mocks as proof of library behavior.

### C6. Enforce allocation policy before recovery or bindings

- [ ] **Integration gate; hours to 3 days.** Read ADR 0015 before adding a catching engine
  wrapper, Python binding or recovery loop. Choose an enforced fatal boundary or exception-safe
  rollback with explicit process behavior.
  **Done when:** injected failures prove the chosen behavior; no retained book can be reused
  corrupt. Include Portfolio: execution-ID insertion currently follows account-state updates, so
  a caught allocation failure must not permit double application on retry. Integer-overflow tests
  do not cover this path.

### C7. Extend financial guards alongside accounting

- [x] **Done 19 September 2026.** `src/engine/portfolio.cpp` and `src/engine/risk.cpp` are now in
  the guard's exact-financial set, alongside the headers that were already there. Declaring a type
  in an int64 header never stopped the arithmetic in the matching `.cpp` from passing through a
  `double`, which is precisely where a rounding error would enter.

  Listed **before** those files exist, deliberately. The set is matched against files actually
  walked, so an entry for a missing file is inert until the file appears — which means the guard
  is in place the day Portfolio is written rather than retrofitted after. Also renamed
  `isExactFinancialHeader` to `isExactFinancialPath`, since it no longer covers headers only.

  Verified both directions: a `double` planted in `src/engine/portfolio.cpp` produces violations
  and exit 1, while a `double` in `src/research/statistics.cpp` is still allowed, so probabilities
  and statistics keep floating point. Guard passes on the real tree; 13/13 Python tests pass.

- [x] September review follow-up: extend coverage to `src/book/` and add a regression test for
  book, Portfolio and risk implementations. The guard remains lexical, not an arithmetic proof.

### C8. Review documentation repairs (review item 10)

- [x] Align README/CLAUDE build paths with the handoff; describe the actual floating-point guard;
  label planned directories, record the supplied PhD topic and qualify the tape timing limitation.
- [ ] Resolve accounting wording with D2: rounding/remainder selection, zero-basis assumptions,
  derived-average error bound and eight decimal places meaning hundred-millionths, not billionths.
  **Done when:** ADR, tests and checklist agree; no unimplemented guarantee is described as proved.

Verification for this small-fix batch: normal C++ build passed; 328 cases passed, one scaffold
skipped and 15 specifications disabled. Python: 14 passed; architecture guard, changed-document
links and `git diff --check` passed. This does not complete Portfolio or the open capture gates.

### C9. Build and interface boundaries (review item 23)

- [x] Move fixture/root-path definitions from `te_core`'s public interface to private test targets.
- [ ] Before an optimized book or another consumer depends on internals, review public locator/
  handle types and dependency visibility. Split targets only when a real independent consumer needs it.
  **Done when:** consumers use the supported interface without depending on reference-book storage.

## D. Build the Stage 5 engine

### D1. Agree the money rules using examples

- [x] **Done 19 September 2026. Twelve rules selected and recorded in ADR 0014 D5.**

  Worked by hand so far, each row checked against equity and against realized-plus-unrealized:
  open long, partial close, final close, open short, partial cover, final cover, and a
  long-to-short reversal selling 5 while holding 2.

  Selected: signed `int64` money at a per-instrument scale finer than the quote currency's minor
  unit; total basis plus quantity with the average derived, never stored; net fee convention;
  fees always moving basis against the trader; classification by signed position; proportional
  reversal fee split; the recorded zero-basis invariant (challenged by the review; see D2);
  realized produced only by closing.

  Also selected: money at **8 decimal places** below one quote-currency unit, chosen from the
  smallest representable amount rather than the ceiling — one satoshi at $100,000 is `$0.001` and
  rounds to zero in whole cents. Duplicates keyed on the **venue's execution ID**, ignored with no
  state change and retained for the whole run. Overflow **refuses the fill** with a named error,
  using a 128-bit intermediate for `price x quantity` and a candidate-then-commit update so a
  half-changed account is unrepresentable. Partial closes **subtract what left** rather than
  rebuilding from a rounded average — rebuilding multiplies the rounding error by the remaining
  quantity and invents money, while subtracting conserves the total by construction and hands back
  the position-zero-implies-basis-zero invariant for free.

  Short-to-long by buying is deliberately not hand-worked — it is the structural mirror of the
  reversal already done, and belongs in a test rather than a paper row.

  Historical selection is intent, not proof. The review found arithmetic details requiring
  clarification before dependent D2 tests; those follow below. The fee schedule remains separate.

### D2. Implement Portfolio and its fill journal

Review items 1-4 and 8. Keep the existing representation; clarify its edge cases rather than
silently choosing policy in a test. Assistant: tests/review. Fuaad: rules and implementation.

- [x] Correct the two mark-valuation tests to use BTC quantities scaled by 100,000,000.
- [x] Mark the empty closing-test scaffold skipped so it cannot pass without assertions.
- [ ] Agree examples for indivisible basis allocation, reversal-fee remainder assignment,
  zero-basis/nonzero-position cases and permitted rebates. State the rounding operation explicitly.
  **Done when:** every example has exact integer inputs/outputs and a named rule; update C8 wording.
- [ ] Replace the skipped closing scaffold with assertions from the agreed partial-close example.
  **Done when:** it checks state and outcome and fails on the current unsupported transition;
  avoid maintaining two copies of the same specification case.
- [ ] Implement checked candidate arithmetic for cash, position, basis, realized and fees;
  validate supplied notional under the agreed contract. Use wider multiplication when converting units.
  **Done when:** boundary/invalid inputs return named errors with no account or execution-ID change.
- [ ] Implement long reductions, flat closure, shorts, covers and both reversal directions;
  enforce duplicate identity, marked valuation and journal replay. Enable specifications as implemented.
  **Done when:** long/flat/short/reversal cases pass; failed and duplicate fills cannot half-change
  state; journal replay rebuilds the account exactly. Fill accounting obtains no mark or wall clock.
  Allocation-failure behavior is the separate C6 integration gate, not proved by integer checks.

### D3. Define intentions, reasons and the strategy boundary

- [ ] Add only required value types and read-only, lifetime-bounded book access.
  Strategy observation consumes successfully processed inputs; decisions return intentions
  without execution authority. Do not introduce Stage 9's `OperationalState`.
  **Done when:** trust/shape blocks have distinct tested reasons and `NoopStrategy` uses the
  real interface without producing an order. Settle ADR 0009 when the seam needs it.

### D4. Build the simulated venue and real admission checks

- [ ] Support submit, accept, reject, partial/full fill, cancel request and acknowledgement.
  Count accepted outstanding orders in worst-case exposure; do not assume opposing orders fill together.
  Implement quantity, notional and resulting absolute-position limits.
  **Done when:** transitions are independently testable, rejection cannot mutate venue/portfolio
  state, and a fill can correctly race a pending cancellation.
- [ ] **Review item 14:** choose how hypothetical fills consume/reserve liquidity without corrupting
  the historical reconstruction book. **Done when:** repeated aggressive orders cannot reuse liquidity
  silently; full/partial fills and multi-level walks have explicit non-impact assumptions.

### D5. Implement scheduling and information availability

- [ ] Keep one thread and inject feed/clock/strategy/venue dependencies.
  Preserve metadata for zero, fixed (80 ms) and recorded availability policies.
  Specify ties and how delayed/out-of-order observations preserve a valid strategy view.
  **Done when:** timelines prove apply-before-observe, no future information, market-before-own-arrival
  ties, submission-sequence ties and arrival-time fill eligibility.
  Stored receipt time is deterministic input, not a calibrated network-delay measurement.
- [ ] **Review items 13/17:** separate feed availability from order-entry delay; retain required
  timestamps through normalization and versioned tape when supporting recorded availability.
  **Done when:** an order cannot fill against liquidity removed before its arrival; raw/tape
  equivalence holds for supported policies. Zero/fixed policies do not require recorded timestamps.

### D6. Pass the complete engine proof

- [ ] Wire feed, book, health, strategy, gate, admission, venue and portfolio.
  Close C2 before trusted capture runs and C6 before exception recovery.
  **Done when:** no-op conservation, a hand-calculated scripted fill, a real unchanged-state risk
  rejection and ten repeated runs pass. Save partial-fill, outstanding-exposure, duplicate-fill
  and cancel-race cases. Compare intermediate event/order/account state, not only final totals.

### D7. Ship the replay executable and run record

- [ ] Create `apps/replay_main.cpp` and connect it to the build and actual engine.
  **Done when:** one command runs a mandatory fixture without private data and reports counts,
  trust, shape, blocks/rejections, orders, accounting and fingerprints.
  Record input hashes, commit/build and dirty state, scales, policies, configuration,
  exclusions and result hashes. Detailed traces are optional.
  Refresh this checklist and handoff with actual build/test evidence.

## E. After Stage 5: learning and research gates

Planned, not implemented; detailed requirements live in plan v4.
Longer-range context, including the PhD bridge, is proposed in
[research-extension-plan.md](docs/research-extension-plan.md) — not accepted, and not a
prerequisite for anything here.

- [ ] **E1. Experiment runner — do this first, immediately after Stage 5.** Pulled forward from
  Stage 10, because every study below is only worth as much as its reproducibility, and results
  produced before the harness exists have to be redone afterwards.
  One command reproduces a result from recorded data, parameters and code version. Plan v4 §4's
  boundary is unchanged — Python configures, reports and analyses; C++ owns the book, fills, risk
  and ledger. This applies that rule earlier, it does not rewrite it. Every run records dataset
  identity and exclusions, parameters and seeds, commit/build identity and dirty state, and
  results. Start with Python invoking the executable and reading structured output; add bindings
  only when repeated-call or transfer cost justifies the added lifetime complexity.
  **Done when:** one command regenerates a comparison report from a mandatory fixture with no
  private data, and a second machine reproduces the same numbers from the same recorded inputs.
  Effort: 2-4 days.

- [ ] **E2. Reproducible failure and recovery experiments.** Inject missing, duplicated, delayed
  and stale inputs, an overloaded consumer, an interrupted run and an invalid model output — each
  from a recorded scenario rather than a live accident. Market feeds and process sensors produce
  the same failure shapes, so this transfers; the recovery rules do not.
  **Done when:** a recorded failure replays to the same defined response every time, with
  diagnostics that identify what failed and where, and a recovered state that is consistent rather
  than merely non-crashing. Effort: 2-4 days.

- [ ] **L3 evidence before Stage 6/8 claims:** compare order IDs/quantities at intermediate
  checkpoints, and priority only where known. Keep the L2 digest for depth checks. Effort: 1-3 days.
- [ ] **Tape equivalence before replacing raw replay:** resolve pre-seed classifier warm-up,
  bind source lineage and preserve or explicitly reject unavailable timing policies.
  Prove raw/tape equivalence for each supported policy. Effort: 2-4 days; format work may add time.
- [ ] **Stage 6:** observed-order labels, explicit cancellation/ambiguity/censoring,
  availability-safe features and transparent baselines.
- [ ] **Stage 7:** chronological sessions, label-interval leakage protection, frozen evaluation,
  calibration, block uncertainty and sensitivity to assumptions.
  This is the trading uncertainty study: does an observed order fill within a defined horizon?
  It must beat a stated naive baseline, and its claimed uncertainty must be checked against
  observed error rather than asserted. Split by session — splitting neighbouring timestamps
  leaks the answer and produces a reassuring number that means nothing.
- [ ] **Stage 8: measured low-latency processing.** Follow S8.1-S8.7 below after Stage 5
  and E1; this expands the existing performance laboratory, not a second engine project.
- [ ] **Stage 9:** read-only live feed, recovery, paper lifecycle, operational risk,
  bounded telemetry and shutdown/failure runbooks. No real-money trading.
- [ ] **Stage 10:** thin dashboard, ownership-safe C++/Python boundary, reproducible
  correctness/performance/research reports and a five-minute demonstration.

### Additional review exercises: after Stage 5 and E1

These extend existing stages, not the definition of a finished D2. Pick one extension at a time.

- [ ] **E3. Fuzzing and fault tests (19).** Exercise malformed JSON/binary records and injected
  failures. **Done when:** bounded harness runs have reproducible seeds/corpora, saved regressions
  and defined rejection/state behavior; sanitizer findings fail the run. Effort: 2-4 days.
- [ ] **E4. Batch Python bindings (24).** Follow E1's CLI baseline; justify bindings by a measured
  call/transfer cost or required API. **Done when:** batch output matches CLI output, ownership and
  GIL behavior are tested, and C6 is satisfied. Effort: 2-5 days.
- [ ] **E5. Execution experiments (25).** TWAP first, then simple inventory-aware market making.
  **Done when:** arrival-price implementation shortfall, fees, inventory limits and sensitivity to
  fill/latency assumptions are reported against a baseline. Effort: 1-2 weeks.
- [ ] **E6. One protocol exercise (27).** Choose UDP loss/reordering or a small ITCH decoder.
  **Done when:** generated loss/duplicates/reordering or truncated packets receive defined responses;
  normalized replay stays deterministic. Do not implement every protocol. Effort: 1-2 weeks.
- [ ] **E7. Backfill or second venue (28).** Investigate documented API coverage and access first.
  **Done when:** a bounded fixture demonstrates verified continuity after recovery, or a second adapter
  passes the same normalization contract. No silent relaxation of admission. Effort: days to weeks.
- [ ] **E8. PhD bridge (29).** With the supervisor, select a simple approximate drying model and data.
  **Done when:** physics-only, data-only and hybrid comparisons share documented splits, baselines,
  metrics and provenance, with uncertainty and limitations reported. Keep domain models separate;
  reuse experiment discipline. Research-dependent; no novelty or winning model assumed.
- [ ] **E9. Advanced infrastructure (30).** Optional FIX connectivity, interactive agent simulation
  or kernel-bypass study only for a selected specialization. **Done when:** a written need, bounded
  exercise and measurable success criterion justify the work. Not an internship prerequisite.

Advanced techniques may be contained learning experiments. Promotion to the main engine requires
correctness equivalence and measured value. A measured non-improvement still teaches something.

### Stage 8 sequence: low-latency evidence

Plan v4 section 18 owns the measurement contract; this is the active implementation sequence.
No measured latency, speedup or specialist qualification is claimed by adding these tasks.

- [ ] **S8.1 Define the timed paths.** Separate decoded-event-to-book-update,
  decoded-event-to-intention/rejection, and producer-publication-to-consumer-completion.
  **Done when:** start/end points, included work, clock and workload are documented.
  Start with the single-thread book; all three are in-process, not exchange round trips.
- [ ] **S8.2 Establish a reproducible baseline.** Use optimized builds, realistic event mixes,
  small/large books, quiet traffic and bursts. Record p50/p99/p99.9, throughput, offered load,
  allocations, memory, backlog and repeated-run variation.
  **Done when:** a public fixture and command reproduce the experiment, with hardware/build
  metadata and raw samples or documented histogram precision. Do not demand identical timings.
- [ ] **S8.3 Profile one bottleneck.** Investigate allocation, pointer chasing, lookup, copying,
  parsing and logging costs; keep parsing separate from decoded-event timings. Parser/padded-buffer
  reuse is a candidate only if the profile supports it; preserve malformed-input behavior and lifetimes.
  **Done when:** a saved profile supports one stated optimization hypothesis.
- [ ] **S8.4 Compare one optimized book variant.** Retain the reference. Try a measured change
  to storage, indexing or allocation, with explicit capacity/exhaustion behaviour.
  **Done when:** intermediate order IDs/quantities and known priority agree; ordinary rejection
  preserves state; failures meet the chosen allocation policy; memory and latency trade-offs
  are reported. An aggregate depth digest alone is insufficient.
- [ ] **S8.5 Compare concurrency designs.** Single thread versus mutex queue versus bounded
  SPSC, with the same useful work and explicit one-producer/one-consumer ownership.
  **Done when:** a publication/slot-reuse happens-before argument is written; wraparound, ordering,
  object lifetime, saturation and shutdown tests pass;
  bursts and delayed consumers are measured; queue-full behaviour cannot silently lose events.
  Run applicable sanitizers separately and explain acquire/release and cache sharing.
- [ ] **S8.6 Audit measurement bias.** Account for timestamp overhead, warm-up, sample count,
  CPU placement/frequency, background load and instrumentation cost. Use scheduled offered load
  to expose overload rather than waiting for each response before submitting the next event.
  **Done when:** queueing delay and missed/dropped work remain visible, tail estimates state
  their sample support, and batch-average timings are not presented as per-event percentiles.
- [ ] **S8.7 Publish the evidence.** Include reproduction command, workload, hardware, build,
  baseline, profiles, correctness results, before/after distributions and capacity/memory costs.
  **Done when:** report includes regressions and non-improvements, distinguishes in-process
  measurements from network latency, and supports every README/CV number. Shared CI runs
  correctness and benchmark smoke checks; controlled hardware supplies performance comparisons.

## F. Completed foundation evidence

Historical observations, not new test results from this documentation update:

- [x] Exact types/parsing, clock seam, byte codecs and portable v3 segment I/O.
- [x] Move-only reference book, ordinary-error atomicity and trust/shape separation.
- [x] Joined capture, merge/reconciliation, mandatory synthetic fixture and input accounting.
- [x] Reported joined checkpoint agreement: 0 of 4,533 price levels differed.
  This does not prove every order's identity or priority.
- [x] Coordinator byte/hash/count admission, overflow guards and optional-corpus skip repair.
- [x] All four Python test files in CI, hashed C++ archives, GCC/Clang Debug sanitizer
  builds, a Release job and explicit debug-only structural validation.
- [x] ADRs 0014/0015 and prior build-directory repair. Historical logs report 325-case
  toolchain runs and a later 327-case local run on 16 September; cite configuration/date.

[The guide](docs/project-progress-guide.md) explains the work and conditional timeframe.
[The handoff](docs/handoff/status.md) records current source limitations.
