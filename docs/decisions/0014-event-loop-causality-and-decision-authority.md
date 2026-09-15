# ADR 0014: Event-loop causality and decision authority

- **Status:** accepted — all 8 open questions resolved (D1-D8), reviewed together 2026-09-15
- **Date:** 2026-09-09, amended 2026-09-15
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

## Proposed decision

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

Debug builds run the full `validateStructure()` sweep after every successful mutation. Release
replay may run it at declared checkpoints. A future production hot path must use cheap always-on
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

All eight open questions below were resolved in design sessions on 2026-09-14/15 and are recorded
here as D1-D8. This ADR stays **proposed** until D1-D8 are reviewed together and signed off.

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

1. **Venue timestamp is the only ordering clock.** Receipt timestamps are not used for ordering.
   Receipt time varies with local network and machine conditions, so ordering by it would make the
   same input replay differently on different runs — the standard event-time versus processing-time
   distinction from stream processing.
2. **A real market event precedes a synthetic order arrival** at an equal timestamp, so an
   engine-generated order can never be sequenced ahead of history that actually happened.
3. **Two of the engine's own orders break ties by submission sequence number**, lowest first — a
   monotonic per-intent counter. This mirrors the FIFO time priority `PriceLevel` already implements
   for resting orders.

#### Ordering time and availability time are separate contracts

Ordering by venue time says *when an event happened*. It does not say *when the strategy could have
known about it*. Conflating the two silently assumes zero market-data delay, which the capture data
disproves.

**Measured, 2026-09-15**, over five capture segments — `localWallTimestampNanos` minus
`venueTimestampMicros`, per frame:

| stream | n | min | p50 | p95 | p99 | max |
|---|---|---|---|---|---|---|
| orders | 189,375 | 33.2 ms | 75.1 ms | 154.9 ms | 249.0 ms | 1382.7 ms |
| trades | 789 | 33.0 ms | 76.2 ms | 173.2 ms | 210.3 ms | 244.4 ms |

Per-segment medians range 53-96 ms across sessions a week apart. **Zero negative values** across
190,164 samples, so the local clock is not running ahead of the venue's.

Caveat: this is delay **plus unknown clock skew**; the capture alone cannot separate them. The shape
of the distribution is trustworthy; the absolute floor carries whatever constant offset exists
between the two clocks.

Feeding events to a strategy at venue timestamp would grant it roughly 75 ms of lookahead — reacting
before the message could physically have arrived. For a short-horizon strategy that is most of the
opportunity, so results would flatter the strategy for reasons that have nothing to do with it.

**Decision: availability is a declared, swappable policy, kept separate from ordering.**

| Policy | Delay source | Applies to |
|---|---|---|
| `zero` | none | Baseline. Comparing against it measures the lookahead bias directly. |
| `fixed` | one declared constant, **80 ms** | Everything, including synthetic scenarios with no recorded delay. |
| `recorded` | `localWallTimestampNanos` per frame | Real captures only. Faithful, keeps the tail, still fully deterministic because it replays recorded data. |

`fixed` exists because `recorded` cannot serve hand-written test timelines — they have no capture
metadata. Running one scenario under more than one policy and reporting the spread follows the
precedent ADR 0008 already sets for cancel assumptions: where an assumption cannot be settled by
evidence, implement several and report how far the answer moves.

80 ms was chosen over the measured 75 ms median as a declared round number with a small safety
margin above it, not a claim of measured accuracy — revisit if evidence says otherwise.

The active policy **must be recorded alongside every result**, mirroring
`kOrderingPolicyOrderWinsTie` in the v3 segment header: a result whose assumptions are not stamped
cannot be compared against another. This folds into the planned run manifest (TODO section E).

### D5. Fill accounting order, committed atomically (was open question 5)

On a confirmed fill:

1. The venue confirms the fill (price, quantity).
2. Compute the fee.
3. **Buy:** fold execution price and fee into a new weighted-average entry price.
   **Sell:** use the **existing** average entry price to compute realized PnL on the quantity sold;
   the fee reduces that realized PnL. A partial sale does **not** change the average entry price of
   the remaining position, and realizes PnL only on the quantity actually sold.
4. Commit cash, signed position, average entry price and PnL **together**, so no observer can see a
   partially updated portfolio.

Reasoning: acquisition costs belong in cost basis; disposal costs reduce proceeds. The atomicity
requirement matches the failure atomicity `OrderBook` already guarantees for rejected mutations.

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

The remaining half of open question 6 — behaviour during `disconnected`, `gapped` and
`resynchronizing` specifically — stays out of scope, per D1: those states do not exist at Stage 5,
since replay has no live connection to lose.

### D7. A reason becomes a permanent contract only once a test asserts it by name (was open question 7)

Decision-block, admission-rejection and fatal-failure reasons are **not** permanent the moment they
are written. A reason becomes a stable public contract only once a committed Stage 5 test asserts it
by name; until then it may be freely renamed or restructured.

Reasoning: this is the same "don't lock in more than is currently load-bearing" rule this ADR already
applies in D1 to reject a premature `OperationalState`. A name nothing depends on yet can be changed
for free; a name a test checks against cannot. The trigger is deliberately checkable — a test exists
or it does not — rather than a matter of memory or convention.

Consequence: the actual reason taxonomy (naming and testing each value) is TODO item 3's job, not
this ADR's. This ADR fixes only the rule for when a name becomes permanent.

### D8. No crossed-to-corrupted threshold yet; measure first (was open question 8)

No duration threshold is set for escalating a persistently crossed safe checkpoint to
`BookHealth::corrupted`. None may be invented without evidence, and none currently exists.

This does not leave crossed markets unhandled: `DecisionGate` already blocks every decision when
`marketShape()` reports `crossed`, via D6, independent of duration. The open question is narrower —
only when a crossed book is corrupted enough to demand resynchronization, not whether it may be
traded.

Decision: **start recording evidence now, decide the threshold later.** Every crossed safe checkpoint's
duration is logged in the replay report (folding into the run manifest, TODO item 8). No escalation
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
T11 fill -> fee -> average price or realized PnL -> atomic commit      [D5]
```

Ordering throughout uses venue timestamps only (D4.1).

## Open decisions required before acceptance

None. All eight plan-v4 causality questions are answered above (D1-D8), reviewed together against
the rest of this document on 2026-09-15, and accepted.

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
