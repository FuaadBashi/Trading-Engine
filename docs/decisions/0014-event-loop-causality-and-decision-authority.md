# ADR 0014: Event-loop causality and decision authority

- **Status:** accepted 2026-09-15; D5 accounting rules selected 2026-09-19 (12 rules, fee schedule
  deferred to D2)
- **Date:** 2026-09-09, amended 2026-09-15, corrected 2026-09-18, rules recorded 2026-09-19
- **Stage:** 5 (with explicitly deferred Stage 9 controls)

## Context

The engine will use the same strategy logic for deterministic replay and live paper observation.
Before building `Strategy`, `ExecutionVenue` or the engine loop, the project needs one ordering and
authority contract: when state changes, what a strategy observes, when it may decide, who can turn an
intention into an order, and how replay reproduces the same result.

Three facts must remain distinct:

1. `OrderBook::validateStructure()` asks whether the C++ containers and locators agree.
2. `BookHealth::isTrusted()` asks whether the reconstructed state comes from a valid snapshot and an
   uninterrupted, successfully processed stream.
3. `OrderBook::marketShape()` reports the visible best-price relationship: `empty`, `one_sided`,
   `locked`, `crossed` or `open`.

None of these alone grants permission to trade. Treating trust and shape as one enum would create an
ever-growing cross-product; treating either one as the final permission check would allow an
open-but-untrusted or trusted-but-one-sided book to drive an order.

## Accepted decision

The 18 September source review corrected status, clock and accounting overstatements.
The authority/order contract remains accepted. These are requirements for the future engine,
not a claim that Strategy, DecisionGate, Portfolio or ExecutionVenue is implemented.
D5 identified an incomplete signed-position policy on 18 September; the twelve rules selected on
19 September complete it. They are accepted intent, not implemented behaviour.

### Apply before observe

A decoded, classified and accepted market event mutates and reconciles the book before it becomes
visible to a strategy. The strategy observes the resulting post-event view. A rejected event does
not trigger a trading decision.

Every successfully processed event reaches the observation path, even when it leaves the market not
decision-ready. Filtering observation would make stateful indicators consume an incomplete event
stream and silently drift from the engine's market history.

Market shape is evaluated only at a declared safe checkpoint after classification, reconciliation
and all mutations caused by one logical input are complete. An intermediate crossed state is not
diagnosed before that checkpoint. A crossed safe checkpoint blocks decisions and is counted by
reason; whether persistent crossing should also escalate `BookHealth` to `corrupted` is decided by
D8 — not yet, evidence is logged first.

### Observation is not execution authority

A strategy never receives a mutable `OrderBook` or direct access to `ExecutionVenue`. Observation
updates strategy state. When the engine's decision gate permits it, a separate decision call returns
`OrderIntent` values. An intent is data, not an accepted order.

The engine-owned decision gate reads independent inputs; it does not mutate them. This is the full,
eventual shape, once Stage 9 gives operational state real values (D1); at Stage 5 the gate reads only
the first two inputs:

```text
BookHealth trust ----\
MarketShape ----------> DecisionGate -> decision allowed or named block reason
Operational state ---/  (Stage 9 only — see D1)
```

The arrows point into the gate only. `BookHealth`, `OrderBook` and operational-state storage never
write to one another. The controller that owns the event loop may transition each state from its own
authoritative input, but one input does not secretly become the storage for another.

If a decision is allowed, returned intents pass through centralized risk checks and then a final
venue-side defensive check. Every block or rejection is a value with a named reason and is included
in deterministic statistics and the audit trail. A strategy-side `if` is never the safety mechanism.

### Replay owns its control inputs

Operational transitions in replay are scripted inputs in the deterministic event sequence. Replay
does not read wall time or receive unscripted operator actions. A test such as "activate the kill
switch at event N" must encode that transition in the scenario so repeated runs apply it at the same
logical point.

Live operator actions are external control events. If Stage 9 records and replays them, they require
the same provenance and deterministic ordering discipline as market inputs.

### Structural failure is not poor market health

Debug builds run the full `validateStructure()` sweep after every successful mutation.
The current function uses assertions and provides no Release validation. Any future
always-on checkpoint validator must be implemented and tested explicitly. A future production hot path must use cheap always-on
local checks for the exact node, locator, quantity and arithmetic touched by a mutation; it must not
scan the full book after every event.

A failed structural check means an internal implementation defect, not an ordinary market state and
not a recoverable `BookHealth` condition. The engine must fail closed: stop decisions and terminate
the affected run with a fatal `internal_invariant_violation`. Resynchronizing market data is not a
valid repair for code that has already proved its own state inconsistent.

## Stage 5 working scope

Stage 5 must implement enough real behaviour to prove the engine loop and accounting; it must not
construct pass-through safety classes merely to resemble the eventual live system.

Load-bearing Stage 5 code:

- apply-before-observe sequencing;
- unconditional ordered observation of successfully processed events;
- a decision gate that genuinely combines book trust and market shape (per D1, operational state is
  deferred to Stage 9 rather than stubbed);
- `OrderIntent` values and a scripted strategy that emits at least one in the hand-calculated test;
- a deterministic simulated venue with real accept, reject, fill and cancel transitions;
- a small real admission/risk policy sufficient to prove both acceptance and rejection, initially
  maximum order quantity/notional and maximum resulting absolute position;
- exact fees, cash, position, average price and PnL updates;
- named reason counters and deterministic hashes.

Stage 5 is not satisfied by `NoopStrategy` alone. The no-op gate proves conservation and observation;
the hand-calculated scenario must exercise the intention, admission, venue, fill and accounting path.

Explicitly deferred to Stage 9 working code:

- live operator pause/resume and shutdown orchestration;
- order-rate limiting;
- drawdown/loss limits;
- production kill-switch wiring;
- heartbeat and disconnect monitoring;
- durable live audit storage and recovery runbooks;
- a local paper adapter connected to an external venue.

These behaviours may be named and constrained now, but Stage 5 must not contain `return true` risk
stubs or pretend they have been implemented. Either a check has real behaviour and tests, or it stays
absent and explicitly deferred.

## Decisions settled after the first draft

D1-D8 were reviewed and accepted on 2026-09-15. The 2026-09-18 correction identifies
an incomplete generalization in D5 and clarifies D4; it does not reopen all eight decisions.

### D1. Stage 5 has no operational-state type (was open question 1)

`OperationalState` is **deferred whole to Stage 9**. At Stage 5 the decision gate combines exactly
two inputs: `BookHealth::isTrusted()` and `OrderBook::marketShape()`.

Reasoning: Stage 5 replays a finite file. There is no operator to pause it, no kill switch to trip,
and a file cannot disconnect. Every state this type could hold reduces to one unvarying value, so no
test could distinguish it from absent. That is the same condition this ADR already rejects when it
forbids `return true` risk stubs — a check that cannot fail is not a check. The rule is applied to
itself rather than carved out.

Consequence: the three-input diagram above describes the Stage 9 shape. Stage 5 implements the
two-input form, and adding the third input later is an addition, not a redesign.

### D2. An intent enters the latency queue only after both gates accept (was open question 2)

The outbound-latency queue models **transmission time to the venue**, not deliberation time. An
intent therefore enters it only after `DecisionGate` permits the decision *and* the admission/risk
policy accepts the intent. A blocked decision or a rejected intent never occupies the queue, and
neither consumes simulated latency.

### D3. First fill eligibility begins at the simulated arrival timestamp (was open question 3)

An arriving order may fill immediately against the book **as it stands at its arrival timestamp** —
not the book the strategy saw when it decided. The stricter alternative, requiring a subsequent
market event before any fill, was considered and rejected as more machinery than Stage 5 needs.

This is a modelling choice, not a claim of venue realism. It must be stated wherever fill results are
reported.

### D4. Ordering uses venue time only; availability is a separate, measured, declared policy (was open question 4)

1. **Venue timestamp orders market reconstruction**, with the order-before-trade exact-tie
   policy from ADR 0013. Receipt timestamps do not replace that reconstruction key.
   Stored receipt timestamps are deterministic inputs too; only reading fresh ambient clock
   values during replay would introduce run-dependent timing. Availability is a separate schedule.
2. **A real market event precedes a synthetic order arrival** at an equal timestamp, so an
   engine-generated order can never be sequenced ahead of history that actually happened.
3. **Two of the engine's own orders break ties by submission sequence number**, lowest first — a
   monotonic per-intent counter. This mirrors the FIFO time priority `PriceLevel` already implements
   for resting orders.

#### Ordering time and availability time are separate contracts

Venue ordering uses the venue's occurrence timestamps. It does not establish when the
strategy could have known an event. A zero-delay model is an explicit simplifying assumption,
not an inference of physical simultaneity from these captures.

**Measured, 2026-09-15**, over five capture segments — `localWallTimestampNanos` minus
`venueTimestampMicros`, per frame:

| stream | n | min | p50 | p95 | p99 | max |
|---|---|---|---|---|---|---|
| orders | 189,375 | 33.2 ms | 75.1 ms | 154.9 ms | 249.0 ms | 1382.7 ms |
| trades | 789 | 33.0 ms | 76.2 ms | 173.2 ms | 210.3 ms | 244.4 ms |

Per-segment medians range 53-96 ms. No negative differences were reported in 190,164
samples. That observation does not establish clock alignment or the sign of clock offset.

These measurements combine delivery/processing delay with unknown inter-clock offset and
possible clock variation. Without synchronization evidence, neither the absolute floor nor
the 75 ms median is a calibrated network delay. Constant offset preserves within-run shape;
clock drift or adjustments can change it. Compare declared scenarios and report sensitivity
rather than claiming a measured amount of lookahead bias.

**Decision: availability is a declared, swappable policy, kept separate from ordering.**

| Policy | Delay source | Applies to |
|---|---|---|
| `zero` | none | Explicit baseline; comparison measures sensitivity to the declared availability assumptions. |
| `fixed` | one declared constant, **80 ms** | Everything, including synthetic scenarios with no recorded delay. |
| `recorded` | retained receipt metadata per frame | Deterministic recorded observations; cross-clock mapping, ties and inconsistent timing need an explicit policy. |

`fixed` exists because `recorded` cannot serve hand-written test timelines — they have no capture
metadata. Running one scenario under more than one policy and reporting the spread follows the
precedent ADR 0008 already sets for cancel assumptions: where an assumption cannot be settled by
evidence, implement several and report how far the answer moves.

80 ms remains the accepted round-number scenario. It is neither a measured delay nor a safety bound.

The active policy **must be recorded alongside every result**, mirroring
`kOrderingPolicyOrderWinsTie` in the v3 segment header: a result whose assumptions are not stamped
cannot be compared against another. This folds into the planned run manifest (TODO D7).

Current C++ envelopes and v3 records do not retain all required receipt metadata. Before
implementing recorded availability, specify metadata preservation, cross-clock mapping,
delivery order and strategy-view lifetime. Delaying a callback must not expose a newer book
containing other unavailable events. Market reconstruction ordering does not by itself solve
this scheduling problem.

### D5. Atomic fill accounting and signed-position rules

**Correction, 2026-09-18:** the earlier "Buy establishes basis; Sell realizes PnL" rule
described opening/closing a long only. It is not a complete specification for TODO's
long, flat, short and reversal requirements. No Portfolio implementation exists yet.

The accepted atomicity requirement remains: calculate and validate the entire candidate
fill update before committing cash, signed position, basis, realized PnL and fee records.
An arithmetic error or duplicate execution cannot leave a partial update.

Classify a fill against the **existing signed position**, not direction alone:

| Existing position and fill | Economic action |
|---|---|
| Flat/long plus buy | Open/increase long |
| Long plus sell up to its size | Reduce/close long |
| Flat/short plus sell | Open/increase short |
| Short plus buy up to its size | Reduce/close short |
| Sell larger than the current long | Close long, then open remaining short |
| Buy larger than the current short | Close short, then open remaining long |

The original convention capitalized opening fees into long basis and deducted closing
fees from realized PnL. Under a corresponding short convention, opening fees reduce
opening proceeds; blindly adding them to short basis is wrong. A gross-PnL convention
with separately reported fees is an alternative, not interchangeable arithmetic.

Unrealized PnL is derived from an explicit mark, position and basis. A fill may change it
through position/basis; the mark itself is not part of the confirmed-fill input.

## Selected accounting rules, 2026-09-19

Chosen by hand-working long, short and reversal examples (TODO D1). Twelve rules are settled;
covering everything D5 listed as required. No `Portfolio` implementation exists yet, so
these are accepted intent, not verified behaviour.

**1. Money representation.** Signed `std::int64_t`, scaled, carried per-instrument alongside the
existing price tick and quantity increment. Unsigned is excluded: cash and position both go
negative, and unsigned subtraction wraps rather than erroring. The scale is finer than the quote
currency's minor unit, because the minor unit is too coarse here -- one satoshi at $100,000 is
0.1 cents, which rounds to zero in whole cents. `btc_usd`, `btc_gbp` and `btc_eur` do not share a
quote currency, so the unit is "minor units of this instrument's quote currency at the declared
scale", never a hardcoded currency. Industry precedent: Nasdaq ITCH carries 4 implied decimals
for US equities whose currency has 2; NautilusTrader uses `int64` at up to 9 decimals or `int128`
at up to 16. Exact decimal count still open (see below). ADR 0004's 128-bit checked intermediate
before rescaling continues to apply to `price x quantity`.

**2. Basis representation.** Store **total basis and quantity** as two integers. The average is
derived for display and never stored, so there is no second number that can drift out of
agreement with the basis. Storing a rounded average was rejected: it re-rounds on every fill and
the error accumulates. An exact rational was rejected because the denominator grows without bound
and is not itself bounded by `int64`; essentially no production financial system stores money
that way.

**3. Gross versus net.** Net. Fees are folded into realized PnL rather than reported as gross PnL
with fees alongside. `docs/project-progress-guide.md` uses the gross convention and is explicitly
a teaching document; the two are not interchangeable, and the worked example makes the difference
concrete -- the same trades gave realized of -3 under net and 0 gross with 3 fees separately.
Do not subtract total fees again from an already fee-adjusted figure.

**4. Fee direction.** A fee never improves a position. On a long it raises what was paid, so it
joins basis. On a short it lowers what was received, so it reduces the proceeds recorded as
basis. Adding a fee to a short's basis is the error the 2026-09-18 correction identified: the
sanity check is that basis must agree with the direction and size of the cash movement it
records.

**5. Classification.** Against the existing signed position, using the table above, never against
buy/sell direction alone.

**6. Reversal fee allocation.** Split proportionally by quantity across the closing and opening
portions of the fill. A 5-unit fill closing 2 and opening 3 assigns 2/5 of the fee to realized
and 3/5 into the new basis. Reason: the books must not depend on whether the venue happened to
report one fill or two. Assigning the whole fee to either side was rejected as arbitrary --
all-to-opening additionally flatters the trade just closed. This division is a further consumer
of the rounding rule still open below.

**7. Zero invariant.** Position is zero if and only if basis is zero. Basis remaining against a
flat position is cost attached to nothing; a non-zero position with zero basis has no record of
what was paid or received. Both are bugs, and this is a cheap always-checkable invariant.

**8. Realized accumulates; opening never realizes.** Realized is a running account total. Only
closing produces realized PnL -- opening a long or a short produces none. Compute a fill's
contribution in isolation first, then fold it into the total.

**9. Decimal scale.** Eight places below one unit of the quote currency, so one stored unit is
`0.00000001` of that currency. Chosen from the low end, not the high end: the ceiling is
comfortable at every candidate scale (8 places still allows about 92 billion currency units,
far beyond any account), so what decides it is the smallest representable amount. One satoshi at
$100,000 is `$0.001`, which rounds to zero in whole cents — whole minor units silently destroy
small fills. Eight places also matches the existing BTC quantity increment, so money and quantity
share a scale, and matches what venues carry in practice.

**10. Execution identity and duplicates.** Identity is the **venue's own execution ID**, not a
locally generated counter — a local counter restarts or drifts across a reconnect, which is
precisely when duplicates arrive. Fees carry their own identity, since a fee can arrive as a
message separate from its fill. A fill whose ID has already been applied is **ignored with no
state change**; it is not an error, because redelivery after a reconnect is normal protocol
behaviour. Applied IDs are retained for the whole run: replay inputs are finite capture files, so
the set has a natural bound. Unbounded growth becomes real only under continuous live operation,
which is Stage 9's problem. Counting ignored duplicates is deliberately deferred — it changes no
behaviour and no contract, so it can be added the day someone wants the visibility. The cost of
deferring is that a flood of duplicates would be silent.

**11. Overflow.** `price x quantity` is computed in a **128-bit intermediate** before rescaling,
per ADR 0004; at an 8-place money scale an ordinary large trade (a $100,000 price against 1000
BTC) exceeds a 64-bit product by orders of magnitude, so this is a routine path, not an edge case.
Where a result would not fit, the fill is **refused with a named error and no state change** —
never wrapped, never clamped, because a clamped figure is silently wrong money that nothing
flags. Enforced by construction: compute the complete candidate (cash, signed position, basis,
realized, fees), validate all of it, then commit in one step. A half-updated account is therefore
unrepresentable rather than merely avoided, matching what `OrderBook` already guarantees for
rejected events and satisfying D5's atomicity requirement above. `TradeReconciler::observe`
saturates instead of refusing; that is a documented exception, forced by its `void` return and
safe because `reconcile` only ever consumes `min(shortfall, credit)`.

**12. Partial-close allocation.** On a partial close:

```
removed   = basis x (units closed / units held)   rounded to the stored scale
remaining = basis - removed                       never recomputed
```

The load-bearing half is the second line. Because `remaining` is *defined* as what did not leave,
`removed + remaining == basis` holds by construction no matter how the division rounded, so the
total can never leak. Rebuilding the remainder instead — rounding a per-unit average and
multiplying it back by the units still held — multiplies the rounding error by that quantity and
invents or destroys money: a one-penny rounding against 1000 units fabricates ten pounds on every
partial close.

Consequences: the total is exact always; the derived average may differ by at most one unit in
the last stored place, and that error does not accumulate because it stays in the residual basis
until the final close settles it. At 8 places that is billionths of a currency unit. This also
supplies rule 7 for free — a final close removes `basis x (all / all) == basis`, so basis reaches
exactly zero as position does, with no special case.

This supersedes the earlier "extra precision versus carried residual" framing. A carried residual
was rejected for the reason recorded before (it is hidden state the fill journal would have to
reproduce on replay), but the deeper point is that subtracting rather than rebuilding already
conserves the total exactly, so no residual field is needed.

### Still open

Fee *schedule* (flat, basis-point, maker/taker) stays out of scope: rule 4 fixes where a fee
lands, not how it is computed. Whether `Portfolio` receives a fee or calculates one is an
interface question for D2. Nothing else in D5 remains unselected.

### D6. `DecisionGate` is invoked at every checkpoint, trusted or not (was open question 6a)

`DecisionGate` is called unconditionally at every safe checkpoint, including while the book is
untrusted. An untrusted book is blocked with a named reason like any other block; the gate is never
skipped as a fast path.

Reasoning: if the engine skipped the call while untrusted, nothing else could produce the
"N decisions blocked, reason: untrusted" count the Stage 5 evidence gate requires — the engine would
either have to duplicate trust-checking logic itself (violating the single-owner policy this ADR
already establishes for `DecisionGate`) or simply not count those blocks at all. Always invoking it
keeps one place responsible for every named reason, at negligible cost (an enum comparison per
checkpoint).

Live disconnect detection and operator control remain Stage 9 work. Recorded gaps,
invalid segments and reseeding still affect replay trust at Stage 5; a finite file can
contain evidence of discontinuity. Integrate the existing BookHealth states and the
admission/segment policy without introducing a placeholder OperationalState.

### D7. A reason becomes a permanent contract only once a test asserts it by name (was open question 7)

Decision-block, admission-rejection and fatal-failure reasons are **not** permanent the moment they
are written. A reason becomes a stable public contract only once a committed Stage 5 test asserts it
by name; until then it may be freely renamed or restructured.

Reasoning: this is the same "don't lock in more than is currently load-bearing" rule this ADR already
applies in D1 to reject a premature `OperationalState`. A name nothing depends on yet can be changed
for free; a name a test checks against cannot. The trigger is deliberately checkable — a test exists
or it does not — rather than a matter of memory or convention.

Consequence: the actual reason taxonomy (naming and testing each value) is TODO D3's job, not
this ADR's. This ADR fixes only the rule for when a name becomes permanent.

### D8. No crossed-to-corrupted threshold yet; measure first (was open question 8)

No duration threshold is set for escalating a persistently crossed safe checkpoint to
`BookHealth::corrupted`. None may be invented without evidence, and none currently exists.

This contract requires the future `DecisionGate` to block every decision when
`marketShape()` reports `crossed`, via D6, independent of duration. The open question is narrower —
only when a crossed book is corrupted enough to demand resynchronization, not whether it may be
traded.

Decision: **instrument Stage 5, decide the threshold later.** Its replay report must record crossed
safe-checkpoint durations (folding into the run manifest, TODO D7). This telemetry is not yet implemented. No escalation
logic is implemented until the corpus shows how long real transient crossings actually last. This
mirrors flap-damping in network monitoring (BGP route flapping, Kubernetes liveness-probe failure
thresholds): the debounce window is measured, not assumed.

## Hand-worked partial timeline

Incorporating D1-D8:

```text
T0  input market event selected by deterministic replay ordering
T1  decode and classify
T2  apply/reconcile all mutations caused by that logical input
T3  debug structural validation
T4  safe checkpoint: compute book trust and market shape        [D1: no operational state at Stage 5]
T4a availability policy applied: zero / fixed(80ms) / recorded, declared per run    [D4]
T5  strategy observes the post-event BookView regardless of decision readiness
T6  DecisionGate always runs; records a block reason (incl. while untrusted) or asks to decide  [D6]
T7  strategy returns zero or more OrderIntent values
T8  admission/risk policy accepts or rejects each intent by reason
T9  accepted intent enters the outbound-latency queue           [D2: only after T6 and T8 accept]
T10 arrival at the simulated venue; eligible to fill at this instant   [D3]
      - a real market event at the same timestamp applies first        [D4.2]
      - two own orders order by submission sequence number             [D4.3]
T11 identified fill -> fee/basis/realized calculation -> atomic account commit      [D5]
```

This is a logical dependency trace, not yet a complete timestamped scheduler example.
Market reconstruction uses venue time; strategy delivery uses availability and own-order
arrival uses outbound delay. Expand the trace into concrete delayed-observation examples
before implementing the scheduler.

## Remaining implementation specifications

Acceptance of the original D1-D8 authority/order decisions stands. Complete D5's accounting
rules and D4's metadata/delayed-view scheduling details before their respective implementations.
These are scoped completion tasks, not a reason to restart the entire ADR.

## Consequences

- Replay and live observation share strategy semantics without giving strategies execution power.
- Book trust and market shape remain independently observable and testable; operational state joins
  them as a third independent input when Stage 9 gives it values worth testing (D1).
- One decision module owns cross-cutting permission policy, improving locality and preventing every
  strategy from rebuilding it differently.
- Stage 5 exercises genuine order and accounting behaviour without pulling the full Stage 9
  operational system into the critical path.
- Replay determinism requires control transitions to be data, not ambient wall-clock effects.
- Internal structural corruption fails closed instead of being mislabeled as a recoverable feed gap.
