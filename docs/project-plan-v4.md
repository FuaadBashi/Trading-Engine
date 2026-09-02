# Trading Engine Project Plan v4

**Owner:** Fuaad Bashi  
**Revised:** 2026-08-28  
**Primary career target:** Quant developer, with credible C++ market-data and performance-engineering evidence  
**Status:** Current source of truth

This plan supersedes `project-plan-v2.md` and `coding-plan-v3.md`. Those documents remain useful
historical records of how the design changed, but this document controls current scope, ordering and
exit gates.

## 1. Project identity

The project is no longer best described as simply a paper-trading application. Its stronger and more
accurate description is:

> A deterministic, gap-aware L3 market-data replay and execution-research system that reconciles
> order and trade streams, validates reconstructed state against the venue, compares reference and
> optimized C++ implementations, and evaluates fill forecasts on held-out real sessions.

The final system still includes historical replay, live read-only observation, local paper orders and
an interactive dashboard. Those features demonstrate integration. They are not the primary proof
that the engine is correct or that a quantitative model is useful.

The portfolio case is built from three independent kinds of proof:

1. **Correctness proof:** captured data is provenance-preserving; replay is deterministic; gaps are
   fail-closed; reconstructed state agrees with independent venue checkpoints.
2. **Quantitative proof:** labels come from observed order/trade outcomes; features contain no future
   information; baselines and models are evaluated on held-out sessions with calibration and
   uncertainty.
3. **Systems proof:** a clear reference implementation is profiled; any optimized implementation
   replays identically; latency, throughput, allocation and hardware-counter claims use a documented
   method.

The target interview story is:

> I built a reproducible L3 market-data and execution-research system. I verified reconstruction
> against venue state, designed deterministic gap recovery, compared reference and optimized C++
> implementations, measured tail performance, and evaluated fill forecasts out of sample with
> explicit uncertainty.

## 2. Current verified baseline

The status below reflects the repository on **2026-08-28**. A clean build passes **273 CTest cases**
plus 8 Python tests. Six C++ tests skip when private capture data is absent -- see the fixture gap
below, which is why a green run on a fresh checkout does not yet mean much.

`docs/handoff/status.md` is the short companion to this table and is refreshed more often.

### Implemented and tested

| Area | Existing implementation | Evidence |
|---|---|---|
| Build foundation | CMake C++20, `te_core`, thin application target, warnings, pinned dependencies | GCC and Clang Linux CI |
| Runtime checking | AddressSanitizer and UndefinedBehaviorSanitizer configuration and learning laboratory | CI builds both compilers with ASan/UBSan |
| Domain values | `Price`, `Qty`, `OrderId`, `Side`, `InstrumentSpec`, representation contracts | Unit and compile-time tests |
| Exact parsing | Exact fixed-point decimal and unsigned integer parsing with boundary/overflow rejection | Decimal and integer test suites |
| Venue decoding | Bitstamp L3 order decoder and trade decoder | Real-message and malformed-input tests |
| Capture | Snapshot-backed Bitstamp order segments, manifests, validation, negative fault corpus | Python capture validator and measured captures |
| Legacy recording | v2 `Record`, `Sink`, recorder counters, gap markers and byte round trips | Golden recorder tests |
| Snapshot parsing | Bitstamp `group=2` L3 snapshot parser | Synthetic and real-snapshot tests |
| Event filtering | Bitstamp zero-price lifecycle classifier | Reason-counted classifier tests |
| Reference book | `PriceLevel`, move-only `OrderBook`, add/modify/remove, locators and invariants | Focused book tests |
| Bootstrap | Seed a fresh `OrderBook` from a parsed venue snapshot | Bootstrap tests |
| Venue checkpoint | Replay order events from one snapshot to a later independent snapshot | Real golden replay test |
| Fill correction | `TradeEvent`, trade decoder, `TradeReconciler` fill ledger keyed by `(orderId, venueTimestamp)` | Partial/full fill lifecycle tests; ADR 0013 |
| Joined capture loading | `loadJoinedCapture`: manifest + payload/frame positional join, decodes `amount_traded` and `captureOrdinal` | Synthetic fixture tests plus one real 29k-event capture |
| Merge controller | `te::bitstamp::Replay`: two-pointer merge by venue time, order-wins-tie, full input accounting | 16 unit tests plus a real-corpus zero-residual test |
| Determinism fingerprints | `OrderBook::digest()`, `ReplayStats::appliedEventDigest` | Route-independence and ten-run identical-digest tests |
| Documentation | ADRs, learning notes and generated PDF deep dives | Repository documentation |

### Important work that is not complete

| Gap | Why it matters |
|---|---|
| **Hermetic end-to-end golden fixture** | **The blocker.** A fresh checkout reports every test green while silently skipping every real-corpus test, including the correctness gate. Closing this is what makes all later Stage 3 evidence worth anything. |
| Correction path unreached on real data | 637 of 637 fills against a resting order were already reported by `live_orders`, so `TradeReconciler` has never fired on a real capture. Insurance, not a validated path -- ADR 0013. |
| Three unexplained golden-replay orders | Previously assumed to be silent full fills. That explanation is now unlikely; they remain unexplained and cannot be diagnosed from an order-only historical capture. |
| Book health states | `unseeded -> warming -> valid -> stale_or_gapped -> resyncing -> valid` does not exist. No strategy gating. |
| Replay-side gap/reseed policy | The *capture* side now refuses a seed that predates its stream. What replay should do on meeting a gap is still undecided (ADR 0013, deliberately deferred). |
| Multi-segment capture loading | `loadJoinedCapture` reads only the first segment, so a capture that reconnected is partly unreachable from C++. |
| Nothing replays *from* a v3 tape | `writeEventTape` converts a joined capture into a segment (L1: merge order baked in, classification/book/reconciliation still run on read) — built 2026-09-02. `EventSegmentReader` decodes records but nothing feeds them into an `OrderBook`. Blocked on proving tape/raw replay equivalence: `Replay` warms a stateful classifier on pre-seed orders that a tape does not carry. See `docs/specs/v3-segment-format.md`, "Known gap: classifier warm-up". v3 is a derived accelerator over the raw capture, not the archive — ADR 0011, decided 2026-09-01. |
| Timestamp type contract | Receipt timestamp naming and nanosecond storage disagree; venue/local clock subtraction is not network latency. |
| Two invariant modes | Structural invariants and stable decision-ready book checks must not be treated as identical. |
| Same-venue L2/checkpoint suite | Replay compares one final checkpoint; captures carry a checkpoint per segment. |
| Replay, strategy, ledger, risk and venue interfaces | Slice 3 and later production components are still placeholders. |

### Closed since this table was written

| Was a gap | Closed by |
|---|---|
| One joined order/trade capture contract | `dump_raw_ws_bitstamp.py` + `validate_joined_capture.py`; seed must now cover stream start |
| Production merge/reconciliation controller | `te::bitstamp::Replay`, ADR 0013 accepted |
| Zero unexplained checkpoint residuals | Joined replay matches its S1 snapshot exactly: 0 of 4,533 levels, no hand-listed exceptions |
| `id` and `id_str` agreement | `DecoderError::id_mismatch`, ADR 0005 satisfied |
| Every input accounted for | `beforeSeed + read + afterCutoff == input size`, asserted on the real capture |
| Deterministic digests | `OrderBook::digest()` and `ReplayStats::appliedEventDigest`; ten-run identical test |

## 3. Role alignment

The project deliberately spans a shared core and three role-relevant evidence tracks.

| Role family | What employers look for | Project proof |
|---|---|---|
| Quant developer | C++ and Python, high-fidelity simulation, market microstructure, clean research-to-production path, statistics | Deterministic replay, label dataset, baselines, held-out calibration report |
| Market-data engineer | Protocol decoding, snapshot-plus-delta state, gap recovery, schema evolution, observability | Joined capture, manifests, v3 encoding, checkpoint reconciliation, health states |
| Low-latency C++ engineer | Memory ownership, CPU/cache knowledge, profiling, concurrency and measured trade-offs | Reference-versus-optimized replay, perf counters, allocation counts, SPSC experiment |
| Trading production engineer | Failure recovery, risk controls, idempotency, monitoring and operational ownership | Runbook, fault injection, paper venue, kill switch, non-blocking telemetry |

The primary target is **quant developer**. The market-data and performance tracks make the C++ work
credible without turning the project into an unfocused FPGA, DPDK or infrastructure project.

## 4. Architectural spine

The authoritative C++ engine remains independent of files, sockets and user-interface toolkits.

| Boundary | Historical mode | Live/paper mode |
|---|---|---|
| `Feed` | validated replay source | live Bitstamp source |
| `Clock` | event/injected clock | system and monotonic clocks |
| `ExecutionVenue` | deterministic simulated venue | local paper venue |
| `Strategy` | same strategy implementation/configuration | same strategy implementation/configuration |
| Telemetry | replay events and reports | bounded live health/book/order stream |

The reference `std::map`/`std::list` book is permanent. It is the easy-to-reason-about correctness
oracle. An optimized book may be added later, but it must consume the same normalized event tape and
produce identical checkpoint and final-state hashes before its performance results are accepted.

Python may perform research, reporting and presentation. Python must not independently reconstruct
the authoritative book, calculate fills, manage risk or maintain a second portfolio ledger.

## 5. Ground truth and scientific limits

### Observed-order outcomes are primary

For a real `order_created` event at time T, use only information available at or before T to predict
whether that exact order fills within fixed horizons. Join Bitstamp order and trade streams by order
ID to classify later partial fills, complete fills, cancellations and censoring.

Required targets include:

- filled within 1 second, 5 seconds and 30 seconds;
- time to first fill and complete fill where observable;
- cancellation or censoring before the horizon;
- signed mid-price movement after fill;
- opportunity cost when an order does not fill.

### Hypothetical orders are secondary

A simulated order inserted at the back of a visible queue is a counterfactual experiment. It does
not prove how the real market would have reacted because the hypothetical order may have changed
other participants' behavior.

Every result must name its assumptions: visible liquidity completeness, priority rules, hidden
orders, tied events, replace semantics, latency scenario, fees and absence of market impact.

### PnL is not early proof

Do not headline strategy PnL until the fill model, fees, label quality and leakage controls are
validated. Early strategy scenarios exist to test engine accounting, not to claim trading edge.

## 6. Unified capture and event-time contract

Orders and trades must be captured under one run identity and one manifest. Prefer one process and
one connection when the venue permits it; if multiple connections are required, the capture process
still assigns one shared local arrival ordinal.

Every accepted raw frame receives:

- `venueTimestampMicros`: timestamp provided by Bitstamp;
- `localWallTimestampNanos`: local time useful for provenance and operational correlation;
- `localSteadyTimestampNanos`: monotonic time useful for local duration measurements;
- `captureOrdinal`: one increasing integer shared by order and trade frames;
- `streamKind`: order, trade or control;
- `runId` and `segmentId`;
- raw payload bytes preserved without semantic rewriting.

The deterministic replay key is:

```text
(venueTimestampMicros, order events before trade events)
```

**Amended 2026-08-28 on measurement.** This section originally specified
`(venueTimestampMicros, captureOrdinal)`. That is wrong for this venue. Bitstamp's `live_trades`
frame reaches the socket *before* its matching `live_orders` frame in **394 of 427** shared
timestamps across four capture segments, so ordering by ordinal runs `TradeReconciler::reconcile`
before `observe` has recorded the order — the reconciler is then asked about an order it has never
seen. Replaying the reference capture under the ordinal key produces 41 `OrderBook::apply` failures
where the current rule produces none. See ADR 0013, "`captureOrdinal` is carried, but is
deliberately NOT the tie-break".

`captureOrdinal` is still captured, decoded and carried on every event. It is provenance — evidence
of local observation order, and the only reason the measurement above was possible. It is not the
tie-breaker, and it was never a claim about the matching engine's internal processing order.

An order-chain break invalidates the affected replay segment. A new segment requires a new
snapshot. Replay never crosses a gap or transport boundary while pretending the book remains valid.

## 7. Deterministic merge and reconciliation contract

**Built 2026-08-27** as `te::bitstamp::Replay` (`src/feed/bitstamp/replay.cpp`). It owns stream
ordering, classification, book application, trade correction and reason counters. Health states are
not implemented; see the deferred list at the end of this section.

The ADR this section required is `docs/decisions/0013-merge-ordering-and-fill-double-counting.md`,
accepted. It settles tie-breaking and specifies how corrections avoid double-counting a fill the
venue already reported. Read it before changing this contract.

### Real order event

1. Select the next frame using the declared replay key.
2. Decode it and record the exact decode result.
3. Pass decoded orders through the Bitstamp classifier.
4. If the classifier permits the event, apply it to `OrderBook`.
5. Only after successful application, call `TradeReconciler::observe`.
6. Count any expected warm-up or redundant condition by a named reason.
7. Stop or mark the segment invalid on an unexpected contradiction.

### Trade event

1. Decode the trade and retain its provenance.
2. Ask `TradeReconciler` for zero, one or two corrective order events.
3. Apply every correction explicitly to `OrderBook`.
4. Do not feed generated corrections back through `observe`; the reconciler already updated its
   own shadow state while generating them.
5. Treat correction-application failure as an explicit health event with a documented policy.

### Equal timestamps, late events and gaps

The controller must have an ADR that defines:

- exact tie-breaking;
- whether any reorder window exists;
- treatment of events whose venue timestamp moves backward;
- startup warm-up and snapshot cutoff;
- redundant trade/order lifecycle notifications;
- gap, reconnect and reseed behavior;
- when a `BookView` is safe for a strategy.

### Book health

```text
unseeded -> warming -> valid -> stale_or_gapped -> resyncing -> valid
```

A strategy may make decisions only in `valid`. Structural book invariants may run after every
mutation. Stable-market checks, such as an uncrossed decision-ready view, run only at declared safe
checkpoints after classification and reconciliation.

## 8. Reproducible run manifest

Every replay, benchmark and research result writes a machine-readable run manifest containing:

- Git commit and dirty-worktree flag;
- input run/segment identifiers and SHA-256 hashes;
- schema and decoder versions;
- instrument specification;
- replay ordering policy version;
- strategy, model, risk and latency configurations;
- random seed where randomness is used;
- compiler, standard library, build type, sanitizer state, OS and CPU;
- event counts and every exclusion/error counter;
- final event, book, order, portfolio and output hashes;
- paths and hashes for generated reports.

This is the link between a chart, the exact data that produced it, and the exact executable/config
that ran the experiment.

## 9. Stage roadmap

A stage closes when its gate passes, not when its estimated time has elapsed.

| Stage | Focus | Main deliverable | Exit gate |
|---|---|---|---|
| 0 | Truth and contracts | accurate status, timestamp/ID/CI fixes, mandatory fixture | clean checkout runs required evidence |
| 1 | Joined capture | unified order/trade run with shared ordinal | validator proves one coherent segment |
| 2 | Merge and reconciliation | deterministic controller and health counters | repeated final hash; zero silent failures |
| 3 | Golden correctness | snapshot-to-checkpoint joined replay | zero unexplained residuals; no manual patches |
| 4 | Durable corpus | portable v3 order/trade/snapshot format | cross-compiler semantic and byte golden tests |
| 5 | Replay and accounting | single-thread engine, ledger, fees, simulated venue | hand-calculated conservation scenarios pass |
| 6 | Queue labels and baselines | versioned labels, queue model and transparent baselines | leakage tests and label-quality report pass |
| 7 | Held-out validation | chronological evaluation with uncertainty | frozen held-out report beats or explains baseline |
| 8 | Performance laboratory | reference-versus-optimized measurement | identical hashes plus defensible benchmark report |
| 9 | Operational paper path | live read-only feed, risk, paper venue, telemetry | recovery/risk/backpressure tests pass |
| 10 | Dashboard and package | interactive local UI plus portfolio evidence | same engine in replay/live and five-minute demo |

### Portfolio checkpoints

- **Checkpoint A - Market-data engineer proof:** Stage 3, with joined replay and venue agreement.
- **Checkpoint B - Quant developer proof:** Stage 7, with held-out label/model evaluation.
- **Checkpoint C - C++ systems proof:** Stage 8, with equivalent reference/optimized results.
- **Checkpoint D - Complete showcase:** Stage 10, with operational path and thin dashboard.

This lets the project become interview-usable before every product feature is complete.

## 10. Stage 0 - truth and contract cleanup

### Deliverables

- Make this plan and the README the only current status sources; label old plans historical.
- Correct receipt timestamp type/name and document the three timestamp meanings.
- Add `id` versus `id_str` mismatch rejection and tests.
- Split structural and decision-ready validation contracts.
- Activate narrowly scoped clock and floating-price CI guards instead of claiming commented TODOs
  are enforced.
- Add CTest labels for unit, golden, corpus, sanitizer and benchmark categories.
- Create one small mandatory snapshot/order/trade/checkpoint fixture. Check venue redistribution terms
  before committing raw public data; otherwise use a deterministic download with a pinned hash plus
  a committed synthetic fixture.

### Gate

A fresh checkout builds with GCC and Clang, runs every mandatory test without private local files,
and has no documented CI claim that is merely a comment.

## 11. Stage 1 - joined order/trade capture

### Deliverables

- One capture process assigns one shared ordinal to order and trade frames.
- One run manifest binds snapshot, order frames, trade frames and control events.
- Raw payloads remain immutable; derived/normalized files are separate.
- Validator checks order chain, monotonic ordinal, required metadata, counts, sizes and hashes.
- Capture logs control events and reconnects without confusing them with market-data gaps.
- At least one short capture contains a start snapshot, continuous joined stream and later checkpoint.

### Gate

The validator produces a machine-readable report proving one coherent, snapshot-backed joined
segment, including exact counts and reasoned exclusions.

## 12. Stage 2 - merge and reconciliation controller

### Learning focus

- multi-stream merge algorithms;
- deterministic total ordering;
- state machines and ownership;
- failure atomicity;
- event provenance and reason counters.

### Tests first

- order earlier than trade;
- trade earlier than order;
- equal timestamp resolved by capture ordinal;
- partial and full fills;
- redundant later delete after a trade correction;
- unknown IDs and contradictory quantities;
- late/backward timestamp according to policy;
- gap invalidates health and blocks strategy delivery;
- reseed restores a valid book;
- repeated run produces identical digest.

### Gate

The controller reports every input exactly once as applied, skipped, corrected or failed. Ten runs
over the same fixture produce identical event and final-book hashes.

## 13. Stage 3 - correctness closure

### Required evidence

- Replace the current hard-coded three-order checkpoint adjustment with joined trade reconciliation.
- Fail on unexpected `OrderBook::apply` results rather than ignoring them.
- Compare several intermediate checkpoints where data is available, not only one final state.
- Commit or reproducibly obtain a small mandatory golden fixture.
- Maintain a separate full-corpus validation command for large local captures.
- Add model-based/property tests against a deliberately simple independent book oracle.
- Fuzz malformed order/trade/snapshot decoders and generated valid lifecycle sequences.
- Add corruption, truncation, duplicate, reorder and reconnect fault cases.
- Test populated `OrderBook` move construction/assignment and failure atomicity.

### Reconciliation report

Every run reports:

- expected and reconstructed orders, levels and quantities;
- differences explained by trades;
- expected warm-up differences;
- ambiguous differences;
- unexplained differences.

The target is zero unexplained residuals. If venue behavior prevents that, report a measured residual
rate and the precise known limitations instead of hiding it with a broad tolerance.

## 14. Stage 4 - portable v3 tape segments

Implement ADR 0011 after the joined event schema and timing fields are stable.

**Reframed 2026-09-01.** v3 is a *derived accelerator*, not the durable corpus. Raw captures are the
archival truth and are kept permanently; a v3 segment is a regenerable, pre-merged projection of one
of them, and deliberately carries no `captureOrdinal`, local timestamps, `runId` or `segmentId`.
Provenance questions are answered from the raw capture. Because segments are regenerable, the merge
policy is baked into the bytes and stamped in the header. See ADR 0011 and
`docs/specs/v3-segment-format.md`.

### Format requirements

- explicit little-endian headers and fields;
- no whole-struct `memcpy` as the durable format contract;
- order, trade, snapshot and declared boundary record kinds;
- exact field widths, versions and declared sizes;
- manifest counts, byte sizes and SHA-256 bindings;
- reader rejects unsupported version, bad magic, impossible sizes, truncation and hash mismatch;
- legacy v2 reader remains isolated and clearly labelled legacy;
- semantic golden tests and exact-byte golden tests are separate.

### Gate

The same committed fixture decodes to identical semantic events under GCC/Clang and macOS/Linux.
Exact golden bytes do not depend on `sizeof`, padding or host endianness.

## 15. Stage 5 - deterministic replay and accounting

Begin single-threaded. Concurrency is not allowed to complicate correctness.

### Engine contracts

- normalized `MarketEvent` envelope with provenance;
- injected `Feed`, `Clock`, `Strategy` and `ExecutionVenue`;
- explicit event-loop causality ADR;
- immutable or lifetime-bounded `BookView` for strategies;
- cash, position, average price, realized/unrealized PnL and fees;
- deterministic simulated/paper venue;
- periodic state digests for locating divergence;
- run manifest for every replay.

### Causality questions that must be answered

1. When does an input event become visible?
2. When does the book mutate?
3. When does the strategy receive the new view?
4. When does a submitted order enter the latency queue?
5. When can it first fill?
6. When are fees, cash, position and PnL updated?
7. How are equal timestamps ordered?
8. What happens while the book is warming, stale, gapped or resyncing?

### Gate

- no-op strategy conserves event counts, cash, position and PnL;
- a hand-calculated order/fill/fee/PnL scenario matches exactly;
- ten identical runs produce identical event, book, order, cash, position and PnL hashes.

## 16. Stage 6 - queue model, labels and transparent baselines

### Queue model

- exact visible L3 queue tracking where evidence supports it;
- snapshot same-price ordering marked unknown unless venue documentation proves priority;
- price-moving modify treated using the documented provisional cancel/replace rule;
- fills separated from cancellations using joined trade/order evidence;
- hidden liquidity, tied events and market impact retained as explicit limits;
- L2 fallback reports optimistic, pessimistic and proportional cancel assumptions.

ADR 0008 must be decided from measured joined data rather than generic L2 assumptions.

### Versioned label dataset

Each row records prediction time, instrument/session, features, queue evidence, label horizon, outcome,
censor reason, data version and provenance. Tests must prove that no feature uses information after
the prediction instant.

### Required baselines before ML

1. Unconditional fill frequency by horizon.
2. Empirical rate stratified by spread/depth or queue-ahead bucket.
3. Simple queue-ahead heuristic.
4. Simple logistic or survival model using a small declared feature set.

Useful features may include queue ahead, spread, same-side/opposite-side depth, book imbalance,
recent order flow, trade flow and short-horizon volatility. Every feature needs a clear definition,
unit and event-time availability test.

### Gate

Publish a label-quality report containing eligible, filled, partially filled, cancelled, ambiguous and
censored counts, plus a data dictionary and leakage tests.

## 17. Stage 7 - held-out quantitative validation

### Experiment design

- use capture session/day as the independence unit;
- split chronologically, never randomly mix adjacent events across train and test;
- freeze label and exclusion policies before final evaluation;
- tune only on training/validation sessions;
- perform the final held-out evaluation once after choices are frozen;
- resample confidence intervals by session or time block rather than individual correlated events;
- bind every result to a run manifest and corpus hashes.

### Metrics

- Brier score and reliability/calibration curve;
- log loss as a secondary proper score;
- calibration error with bin counts;
- time-to-fill/survival analysis with censoring;
- session/block bootstrap confidence intervals;
- adverse selection after fill;
- opportunity cost for unfilled orders;
- fee/rebate-aware markouts at fixed horizons;
- breakdown by spread, volatility, side, depth and time of day;
- baseline comparison and feature ablation.

### Honest headline form

> Across N eligible orders in M held-out sessions, the queue-aware model changed Brier score from A
> for the declared baseline to B, with session-block 95% confidence interval [...]. X% of candidates
> were censored by the pre-declared gap/end-of-window policy.

No number appears in a README or CV until it is measured and reproducible.

## 18. Stage 8 - C++ performance laboratory

Performance work begins only after deterministic correctness. The reference book is never deleted.

### Benchmark layers

- decimal/integer parse;
- order decoder and trade decoder;
- merger/reconciler;
- add, same-price modify, price-moving modify and remove;
- whole deterministic replay;
- strategy/risk/accounting stage;
- SPSC experiment only after the single-thread baseline is stable.

### Required measurements

- events per second and bytes per second;
- p50, p99 and p99.9 latency;
- cycles and instructions per event;
- cache misses and branch misses;
- allocations per event and bytes allocated;
- peak resident memory;
- bytes per live order and level;
- realistic event mix and seeded-book size;
- maximum sustainable burst rate and backlog growth;
- gap-to-valid-book recovery time.

### Method rules

- controlled Linux hardware for comparisons;
- Release/RelWithDebInfo build, sanitizers disabled during measurement;
- compiler, flags, CPU, OS, governor, warm-up and sample size recorded;
- repeated runs with distribution statistics;
- benchmark JSON and raw profiles stored with the report;
- shared GitHub runners do not enforce nanosecond regression thresholds;
- every optimized candidate must replay identically to the reference engine.

Add a pool, intrusive layout, dense window or SPSC queue only when a profile identifies a relevant
cost. After concurrency is introduced, run ThreadSanitizer and explicit delayed-consumer/burst tests.

### Gate

Publish a reference-versus-optimized report with identical state hashes and a profile that explains
why each optimization exists. A speedup without semantic equivalence is a failed change.

## 19. Stage 9 - operational live and paper path

### Health and recovery

- read-only Bitstamp live feed first;
- explicit `unseeded`, `warming`, `valid`, `stale`, `gapped` and `resyncing` states;
- heartbeat/staleness monitoring;
- gap closes the decision path immediately;
- reconnect requires new snapshot/bootstrap;
- recovery counters and time-to-valid measurement;
- operations runbook for startup, shutdown, failures and recovery.

### Local paper order lifecycle

- pending-new;
- acknowledged;
- partially-filled;
- filled;
- cancel-pending;
- cancelled;
- rejected.

State transitions are explicit and idempotent. A client order ID prevents accidental duplicate
submission.

### Risk controls

- maximum order quantity and notional;
- maximum absolute position;
- maximum open orders;
- order-rate limit;
- stale/gapped-market rejection;
- price-collar sanity check;
- loss/drawdown threshold in local paper mode;
- kill switch;
- durable reasoned audit record for every rejection.

The same strategy, risk, portfolio and accounting code runs in replay and live-paper modes.

### Telemetry

- versioned book, trade, strategy, order, model and health messages;
- observation and command channels remain separate;
- bounded queues and explicit backpressure/drop counters;
- slow or disconnected dashboard cannot block or corrupt the engine;
- ZeroMQ is optional, not a requirement.

## 20. Stage 10 - thin dashboard and portfolio package

The dashboard is a presentation and inspection layer over the authoritative engine.

### Required modes

1. Historical replay with play/pause, speed control, checkpoints and prediction/outcome inspection.
2. Live read-only book/trade/health observation.
3. Local paper orders against replayed or live market state.

### Required views

- best bid/ask, spread, mid and recent trades;
- aggregated depth and selected L3 queue;
- hypothetical order state and queue estimate;
- cash, inventory, realized/unrealized PnL and fees;
- fill forecast and later observed outcome;
- calibration, time-to-fill, adverse-selection and opportunity-cost plots;
- event rate, latency, gap, stale, reconnect and backpressure indicators.

### Portfolio artifacts

1. **Correctness report:** capture integrity, fault injection, checkpoint residuals and deterministic
   hashes.
2. **Benchmark report:** method, hardware, distributions, profiles and reference-versus-optimized
   comparison.
3. **Research report:** dataset, labels, baselines, leakage controls, held-out metrics and limits.
4. **Operations runbook:** startup, health states, gap/reseed behavior, counters and recovery.
5. **Five-minute demo:** one command runs a mandatory fixture, prints hashes/reports and opens the
   thin dashboard.
6. **Architecture/ADR index:** concise map from every major decision to measured evidence.
7. **Limitations page:** venue uncertainty, counterfactual limits and what was not measured.

Resume bullets contain only measured numbers. Raw benchmark outputs, compiler version, Git commit,
config and capture hashes accompany every plotted claim.

## 21. CI and quality gates

### Required on ordinary changes

- GCC and Clang build/test;
- Debug ASan and UBSan;
- warnings as errors for project code;
- mandatory small golden fixture;
- deterministic fixture hash check;
- narrowly scoped fixed-point and clock-boundary guards;
- formatting/static analysis only when configured to remain low-noise.

### Required when the relevant code exists

- TSan job after concurrency;
- fuzz targets for decimal, integer, order, trade, snapshot and v3 decoders;
- generated valid lifecycle/property tests;
- scheduled/full-corpus validation;
- coverage report used to find untested risk, recovery and error branches;
- benchmark artifacts from controlled hardware.

No skipped private-corpus test is described as public CI correctness evidence.

## 22. Scope discipline

The following remain outside the critical path until the evidence above exists:

- real-money trading;
- external Bitstamp sandbox execution;
- ML signal models before transparent baselines;
- equities or a second L3 venue;
- ZeroMQ specifically;
- Kafka, Kubernetes or microservices for keyword value;
- public hosting, accounts or remote execution control;
- DPDK, FPGA or kernel bypass;
- custom allocators, dense books or lock-free queues without profiling evidence;
- many strategies;
- a highly polished dashboard before the headless engine is validated.

One transparent model, a no-op strategy and hand-calculated scenarios provide more evidence than a
large library of unvalidated strategies.

After the core project, choose only one specialization:

- **Quant specialization:** stronger survival/hazard model and additional regime analysis.
- **Low-latency specialization:** fixed-width protocol decoder plus controlled Linux optimization.
- **Market-data specialization:** second venue adapter proving normalization boundaries.

## 23. Learning contract

This project exists to teach C++, finance and quantitative engineering. For learning-critical code:

1. State the behavior in plain English.
2. Draw ownership, state or event ordering when it is not obvious.
3. Write the smallest failing test.
4. Fuaad writes the first implementation attempt.
5. Review correctness, lifetime, failure atomicity and complexity.
6. Run focused tests, then the full suite and relevant sanitizer.
7. Explain the compiler/runtime result in Fuaad's own words.
8. Record the decision or lesson in the relevant ADR/learning note.

At the end of each stage, Fuaad should be able to explain without the editor:

- the domain problem solved;
- the C++ types and ownership involved;
- what can fail and whether state changes on failure;
- the time and memory complexity;
- how correctness was verified;
- what was measured and what was not;
- the largest remaining limitation.

## 24. Immediate next actions

Do these in order:

1. Make Plan v4 and README status truthful; mark older plans historical.
2. Fix timestamp-unit semantics and `id`/`id_str` consistency before freezing a new schema.
3. Specify the joined order/trade capture record and one shared `captureOrdinal`.
4. Capture and validate one short joined snapshot/order/trade/checkpoint session.
5. Write the event-causality/merge ADR, including exact tie, late-event and health rules.
6. Write synthetic merge-controller tests before the controller implementation.
7. Implement the deterministic controller around existing decoder, classifier, reconciler and book.
8. Replace the manual checkpoint adjustments with joined reconciliation evidence.
9. Create a mandatory small golden fixture and a separate full-corpus validation command.
10. Implement ADR 0011 v3 only after the joined schema and timestamp fields are stable.

Do not start `Strategy`, SPSC, optimization or dashboard work before actions 1 through 9 close.

## 25. Final definition of done

The core project is complete only when all of the following are true:

- raw and binary corpora are provenance-bound, versioned and verified;
- replay refuses to cross gaps and recovers only through snapshot reseeding;
- joined order/trade replay reaches independent venue checkpoints with zero unexplained residuals or
  a precisely quantified documented residual policy;
- repeated runs yield identical semantic, book and accounting hashes;
- a clean checkout runs mandatory end-to-end evidence without private local data;
- observed-order labels and features pass event-time leakage tests;
- held-out quantitative metrics include baselines, calibration and uncertainty;
- optimized C++ results are identical to the reference and use a reproducible benchmark method;
- local paper execution has explicit states, risk controls, audit reasons and a kill switch;
- live and replay modes use the same strategy/risk/accounting engine;
- dashboard failure or slowness cannot affect engine correctness;
- correctness, performance, research, operations and limitations reports are reproducible;
- every headline README/CV claim is backed by a stored artifact and measured number.

## 26. Industry references

- [IMC Quantitative Developer](https://www.imc.com/us/careers/jobs/4778139101): high-fidelity
  simulation, microstructure, latency, C++/Python, statistics and research-to-production ownership.
- [IMC C++ Software Engineer](https://www.imc.com/us/careers/jobs/4282973101): reliable high-performance
  software, automated testing and latency/throughput/simplicity/maintainability trade-offs.
- [Optiver C++ Software Engineer](https://www.optiver.com/join-us/jobs/technology/singapore/c-plus-plus-software-engineer/):
  C++, low-latency components, backtesting and research infrastructure.
- [Jane Street Low-Latency Engineer](https://www.janestreet.com/join-jane-street/position/6254435002/):
  profiling, hardware counters, networking and CPU/cache architecture.
- [Google Benchmark User Guide](https://google.github.io/benchmark/user_guide.html): repeated benchmarks,
  custom counters, memory reporting, JSON artifacts and performance counters.
- [Clang AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html),
  [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) and
  [ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html): memory, undefined-behavior and
  data-race detection.
