# Trading Engine: where you are and what comes next

Updated **18 September 2026** against source baseline `6c2f2f6`.
Stage 8 learning guidance amended **21 September 2026**; current task status is in TODO.
The existing PDF is an older snapshot and does not include this amendment.

You have built the foundations for reconstructing a market. The next milestone is a
complete, small trading run: one intention, a real risk check, a simulated fill and
exact accounting. Then use that engine for quantitative research and measured C++ experiments.

This guide explains the plan. [TODO.md](../TODO.md) alone owns task completion;
[the handoff](handoff/status.md) records source status; [plan v4](project-plan-v4.md)
defines the full scope. Recommendations and unchecked tasks are not implemented features.

## 1. Where you are

| Area | Current position |
|---|---|
| Capture, decoding and exact types | Implemented; semantic admission hardening remains |
| Reference book and joined replay | Implemented; aggregate agreement is not full queue proof |
| Portable binary tapes | Codecs and I/O exist; complete raw/tape replay equivalence remains open |
| Engine, portfolio, strategy and simulated venue | Start of Stage 5; implementation remains |
| Queue research and held-out evaluation | Planned, Stages 6-7 |
| Optimized structures and concurrency | Planned, Stage 8 |
| Live paper operation and dashboard | Planned, Stages 9-10 |

The original foundation repair batch closed on 16 September. ADR 0014 is accepted;
its accounting subsection still needs a signed-position policy before Portfolio is written.
New review findings are follow-ups, not evidence that the earlier repairs never happened.

### The next complete path

1. Read an admitted market event and update the appropriate book state.
2. Deliver only the information the strategy is allowed to know at that time.
3. Ask the decision gate about trust and market shape.
4. Let a permitted strategy return an intention.
5. Check quantity, notional and worst-case outstanding exposure.
6. Let the simulated venue accept, reject or fill at the declared arrival time.
7. Apply the identified fill and fee atomically to the account.
8. Produce a repeatable report with input, policy and result fingerprints.

Stage 5 uses one thread. Operator controls, production kill switches and live recovery
belong to Stage 9. They must not appear as empty classes that always approve.

## 2. Your first learning task: make the money rules concrete

Prices in signed integer ticks are sensible. Cash, price and quantity still represent
different things and need distinct units.

For example, with price in cents per BTC and quantity in hundred-millionths of BTC:

```text
notional in cents = price_ticks * quantity_units / 100,000,000
```

The multiplication can overflow a 64-bit type before the final result is small enough.
Use a checked wider intermediate, then the agreed rescaling and rounding rule.
Some fills produce a fraction of a cent; choose the account's internal money precision
and the boundary at which rounding happens.

### Decisions still needed

- Basis: rounded average, total basis plus quantity, or an exact rational representation.
- Reporting: gross PnL with fees separate, or a fee-adjusted basis/net-realized convention.
- Partial close: rounding and where the residual remains.
- Reversal: divide one fill into closing the old position and opening the new one;
  specify how fees are allocated if basis incorporates them.
- Fees: explicit supplied fee versus a versioned fee policy; rebates if supported.
- Execution identity: what makes a repeated fill a duplicate.

**Total basis is a recommendation to evaluate, not a decision already made.**
It avoids repeatedly rounding a stored average, but partial closes still need a residual rule.

### Work these cases by hand

| Case | What it teaches |
|---|---|
| Open/add long, partial/final sell | Weighted basis and realized versus unrealized PnL |
| Open/add short, partial/final cover | Signed exposure and the correct fee direction |
| Sell through zero into a short | Close the old long, then open the remainder |
| Buy through zero into a long | Close the old short, then open the remainder |
| Average 302/3 currency units | Repeats beyond cents; 201/2 currency units is exactly 100.50 |
| Duplicate fill or arithmetic overflow | Rejection without partial state changes |

Short example: sell one unit at 100 with fee 1, then cover at 90 with fee 1.
Cash changes are +99 and -91, so the net gain is 8. With fee-adjusted basis,
opening proceeds are reduced by the opening fee. Blindly adding that fee to short
basis would give the wrong result.

For each example show cash, signed position, basis, realized PnL and cumulative fees.
Put unrealized PnL on a separate row with an explicit mark price. A fill can change
unrealized PnL through position/basis even though the mark itself is not a fill input.

The earlier gross-PnL teaching example and D5's fee-adjusted example are different
reporting conventions. Both can reconcile to the same equity if applied consistently.
Do not subtract fees twice or mix assertions from the two conventions.

**Learning agreement:** you choose the rules and attempt the implementation. The assistant
writes all tests from the agreed examples, including new-feature tests, and explains them.

## 3. What needs hardening and why

The exact tasks and completion gates are in TODO section C.

| Finding | Why it matters | When it must close |
|---|---|---|
| UBSan may recover after reporting UB | A test can appear successful despite a sanitizer report | Early tooling fix |
| C++ admission omits status/chain validity | Matching bytes do not prove a usable continuous capture | Trusted capture-to-engine runs |
| Tape writer lacks ordering/window checks | Eligible events can be silently omitted on invalid input | Before using generated tapes |
| Receipt metadata is discarded by current envelopes | Recorded availability cannot be implemented from missing inputs | Stage 5 timing integration |
| Tape omits classifier warm-up | Raw and tape classification can differ | Before raw/tape substitution |
| Aggregate digest omits order identity/priority | Equal totals can hide different queue outcomes | Stage 6/8 evidence |
| Allocation failure relies on callers not catching | Future recovery/bindings could retain a corrupt book | Before such integration |
| Coordinator collapses error causes | Large corpus failures become hard to diagnose | Before scaling validation |
| CI Python 3.9 is EOL | Reproducibility needs a supported baseline | Toolchain refresh |
| Float guard excludes future financial .cpp files | Headers alone cannot enforce the accounting rule | Alongside Portfolio/risk |

The sparse standard-library book, single-thread first engine, explicit byte serialization
and separation of book trust from market shape remain defensible choices.

Two distinct comparisons are involved: some findings violate this project's stated
contracts; others are maintenance or testing gaps. Choosing virtual dispatch over templates,
for example, is not a violation of a universal industry rule.

### Keep time concepts separate

- **Venue time:** the timestamp used for market reconstruction ordering.
- **Availability:** when information may influence the strategy.
- **Order arrival:** when an accepted intention reaches the simulated venue.

A quote disappearing at time 108 cannot fill an order arriving at 110 just because
the strategy saw it earlier. A delayed observation must not expose a current book
containing other events the strategy has not received.

Stored receipt timestamps can replay deterministically. Their difference from venue
timestamps includes unknown clock offset as well as delay. The existing 80 ms scenario
is a declared assumption, not a proven network latency or safety bound.

## 4. Make the project teach substantial C++

Use each feature to answer a concrete question and produce evidence.

| Milestone | Topics to learn | Evidence |
|---|---|---|
| Accounting ledger | Strong types, checked arithmetic, invariants, atomic updates | Exact signed-position examples and journal reconstruction |
| Event scheduler | Variants, state machines, ordering, dependency injection | Availability and arrival timelines with no lookahead |
| Reference/optimized book | Ownership, iterator validity, layout, allocators, cache costs | Intermediate order-level equivalence and measured profiles |
| Bounded queue experiment | Atomics, acquire/release, lifetime, cache sharing, backpressure | SPSC versus mutex baseline; burst and delayed-consumer tests |
| Fuzz/fault laboratory | Parsing, undefined behavior, invariants, minimization | Small saved counterexamples and safe rejection |
| C++/Python boundary | Array layout, copying, ownership, Python runtime coordination | Research consumes the C++ engine without a second ledger |
| Live-paper service | RAII resources, cancellation, shutdown, network lifecycle | Disconnect/reseed and cancel/fill race scenarios |

C++20 is enough for these lessons. Add a language feature because it solves a problem,
not to maximize syntax coverage. A future C++23 migration or alternative error type is optional.

A pool, intrusive list or static-dispatch variant may be a **contained learning experiment**.
Record the hypothesis, baseline, correctness result and measurements. Promote it into
the main engine only when it earns its place. A measured non-improvement is useful evidence.
Concurrency experiments follow the stable single-thread Stage 5 baseline.

For performance, start with actual profiles: repeated whole-file hashing, parser setup,
container allocation and cache behavior are candidates to measure, not predeclared bottlenecks.
Keep microbenchmarks separate from end-to-end replay. Measure tail latency, allocations,
memory and throughput on controlled hardware; do not infer speed from container names.

### What makes the low-latency work credible

After Stage 5 and the experiment runner, follow [TODO S8.1-S8.7](../TODO.md#stage-8-sequence-low-latency-evidence).
First measure a decoded event updating the reference book. Profile one bottleneck, compare one
optimized variant, then compare single-thread processing with mutex and bounded SPSC queues.
Keep the reference implementation and prove equivalent order state before comparing speed.

Measure three different paths: book update, event-to-intention/rejection, and queue publication
to consumer completion. The last includes queue residence, but not producer waiting before
publication; record that delay and offered load too. None measures an exchange round trip.

Report median and tail latency, throughput, allocations, memory and backlog under quiet and
burst traffic. A producer that waits for each response can hide overload. Record the machine,
build, timing overhead, warm-up and sample count; use repeated optimized runs without sanitizers.
Batch timings estimate average cost, not individual-event tail latency.

The outcome is a short reproducible report: baseline, profile, change, correctness evidence,
before/after distributions and trade-offs, including changes that did not help. No speedup or
"low-latency specialist" claim is earned by adding a pool or queue alone. The detailed contract
is [plan v4 section 18](project-plan-v4.md#18-stage-8---c-performance-laboratory).

## 5. A focused research direction

Candidate question:

> How much do information delay and queue-observation uncertainty change the accuracy
> of short-horizon fill predictions?

This is a proposed research direction, not an accepted PhD thesis or a novelty claim.
Your PhD topic and weekly availability are still unspecified.

1. Predict outcomes of real observed orders first; treat hypothetical inserted orders as
   separate counterfactual experiments.
2. Define first fill, complete fill, cancellation, ambiguity and observation ending.
   Cancellation is an observed event, not automatically the same as missing follow-up.
   Choose censoring or competing-outcome treatment to match the estimand.
3. Build only features available at prediction time.
4. Split by chronological sessions and check label intervals. A training label must not
   use events from the evaluation interval. Use time/interval boundaries for irregular events,
   not a blindly chosen number of rows.
5. Compare transparent baselines, calibration and regime breakdowns; report uncertainty
   by session/time block and sensitivity to latency/queue assumptions.
6. Freeze policies before final held-out evaluation. Bind reports to corpus hashes,
   code/build identity, configuration and policy versions.

A strong result may show that an apparently useful signal disappears under more realistic
availability. That is still a valuable finding. Publishing a paper needs a literature-defined
contribution in addition to sound engineering.

## 6. Remaining work and planning ranges

Git's first reachable foundation commit is 8 August 2026. At this review, 18 September,
that is **41 calendar days**. Commit dates cannot establish active study hours or future speed.

Most integration, research and operational work remains. Stage numbers are not percentages.

| Remaining milestone | Focused hours |
|---|---|
| Essential contracts and hardening | 20-40 |
| Complete Stage 5 engine and accounting | 80-140 |
| Labels and research evaluation | 100-180 |
| Performance and concurrency experiments | 80-140 |
| Live-paper operation, dashboard and demonstration | 100-180 |
| Total full planned project | 380-680 |

| Weekly commitment | Hardening plus Stage 5 | Full planned project |
|---|---|---|
| 10 hours | 10-18 weeks | About 9-16 months |
| 20 hours | 5-9 weeks | About 4.5-8 months |

These estimates include learning, implementation, tests and rework. They are not deadlines;
novel PhD research and data-collection constraints may extend them substantially.
Re-estimate after Stage 5 using observed effort. You can demonstrate useful intermediate
results before the full dashboard is finished.

## 7. Evidence and references

Source evidence for the current review:

- [Portfolio placeholder](../include/te/engine/portfolio.hpp),
  [engine placeholder](../include/te/engine/engine.hpp), [active tasks](../TODO.md).
- [Capture admission](../src/capture/capture_validator.cpp),
  [manifest reader](../src/capture/manifest_reader.cpp),
  [Python validator](../scripts/validate_joined_capture.py).
- [Replay ordering checks](../src/feed/bitstamp/replay.cpp),
  [tape writer](../src/capture/event_tape_writer.cpp).
- [Captured-event metadata](../include/te/feed/captured_events.hpp),
  [v3 limitations](specs/v3-segment-format.md).
- [Aggregate digest contract](../include/te/book/order_book.hpp),
  [allocation policy](decisions/0015-allocation-failure-policy.md).
- [Sanitizers](../cmake/sanitizers.cmake), [CI](../.github/workflows/ci.yml),
  [financial guard scope](../scripts/check_architecture_guards.py).

Named external comparisons, checked for the 18 September review:

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines):
  ownership, invariants, exception policy and measurement-led optimization; guidance, not the ISO standard.
- [Clang sanitizer behavior](https://clang.llvm.org/docs/UsersManual.html#controlling-code-generation):
  UBSan recovery must be considered when making CI a failure gate.
- [LLVM libFuzzer](https://llvm.org/docs/LibFuzzer.html): coverage-guided parser testing.
- [Google Benchmark](https://google.github.io/benchmark/user_guide.html): repeated measurements,
  counters and machine-readable output.
- [Python lifecycle](https://devguide.python.org/versions/): Python 3.9 ended upstream support
  on 31 October 2025.
- [TimeSeriesSplit](https://scikit-learn.org/stable/modules/generated/sklearn.model_selection.TimeSeriesSplit.html):
  chronological/gap concepts; account for sampling assumptions before applying it to irregular events.

## 8. What this update does not prove

This is a source-grounded plan, not a fresh clean-build certificate.
The review did not inspect every historical Markdown/test body line by line, audit third-party
internals, rerun the complete private corpus, establish live venue behavior, or measure performance
or race freedom. Earlier test counts and checkpoint results remain dated historical evidence.

The editable source is this Markdown file. The matching PDF is
[trading-engine-progress-guide.pdf](../output/pdf/trading-engine-progress-guide.pdf).
Other deep dives and old plan PDFs are historical snapshots. This guide and TODO's PDF are
regenerated together when current guidance changes; the gitignored output directory does not
make a PDF part of a fresh checkout.
