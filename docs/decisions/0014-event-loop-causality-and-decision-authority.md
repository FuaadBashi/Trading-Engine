# ADR 0014: Event-loop causality and decision authority

- **Status:** proposed
- **Date:** 2026-09-09
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
reason; the evidence and threshold needed to classify persistent crossing as lost trust remain open.

### Observation is not execution authority

A strategy never receives a mutable `OrderBook` or direct access to `ExecutionVenue`. Observation
updates strategy state. When the engine's decision gate permits it, a separate decision call returns
`OrderIntent` values. An intent is data, not an accepted order.

The engine-owned decision gate reads independent inputs; it does not mutate them:

```text
BookHealth trust ----\
MarketShape ----------> DecisionGate -> decision allowed or named block reason
Operational state ---/
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
- a decision gate that genuinely combines trust, shape and a minimal replay operational state;
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

## Hand-worked partial timeline

The decisions settled so far imply this prefix:

```text
T0 input market event selected by deterministic replay ordering
T1 decode and classify
T2 apply/reconcile all mutations caused by that logical input
T3 debug structural validation
T4 safe checkpoint: compute trust, market shape and operational state
T5 strategy observes the post-event BookView regardless of decision readiness
T6 DecisionGate either records a block reason or asks the strategy to decide
T7 strategy returns zero or more OrderIntent values
T8 admission/risk policy accepts or rejects each intent by reason
T9 accepted intent enters the simulated venue path
```

The timing and ordering after T9 remain open below.

## Open decisions required before acceptance

1. Exact minimal Stage 5 operational states and transitions.
2. When an accepted intent enters the outbound-latency queue.
3. Whether first fill eligibility begins at the simulated arrival timestamp or only with later market
   activity.
4. Ordering among venue timestamps, receipt timestamps, simulation time, control events and equal
   timestamps.
5. Exact order of acknowledgement, fill, fee, cash, position, average-price and PnL mutations.
6. Observation and decision behaviour while unseeded, synchronizing, corrupted, disconnected,
   gapped and resynchronizing.
7. Stable public taxonomies for decision blocks, admission rejections and fatal engine failures.
8. Evidence-based escalation from a crossed safe checkpoint to lost book trust.

## Consequences

- Replay and live observation share strategy semantics without giving strategies execution power.
- Book trust, market shape and operational state remain independently observable and testable.
- One decision module owns cross-cutting permission policy, improving locality and preventing every
  strategy from rebuilding it differently.
- Stage 5 exercises genuine order and accounting behaviour without pulling the full Stage 9
  operational system into the critical path.
- Replay determinism requires control transitions to be data, not ambient wall-clock effects.
- Internal structural corruption fails closed instead of being mislabeled as a recoverable feed gap.
