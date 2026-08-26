# ADR 0013: Merge ordering and fill double-counting in the replay controller

- **Status:** proposed
- **Date:** 2026-08-22
- **Slice:** 2 (plan v4 Stage 2)

## Context

Project plan v4 §7 requires the merge/reconciliation controller to have an ADR defining tie-breaking
and correction policy. This is that ADR. It is written from a reproducible failure rather than from
first principles, because the first-principles answer turned out to be wrong.

`bitstamp::Replay` merges `live_orders` and `live_trades` by `venue_timestamp_us`, applying the
earlier event and breaking exact ties in favour of the order event. The stated reason for that
tie-break was that `TradeReconciler::observe` must update the reconciler's record before
`reconcile` reads it. Running the controller over the first real joined capture
(`data/raw/bitstamp-btcusd-20260822T000512Z`, 29,404 order events and 84 trades between seed
`1787357113459950` and checkpoint `1787357172661042`) returns
`ReplayError::unexpected_order_apply_failure`.

Nine order events fail to apply. All nine are `ApplyError::unknown_order_id`, across five distinct
order ids, and every one of them targets an id that a trade-derived correction had already removed
from the book between 1,000 and 36,000 microseconds earlier. There is one failure mode, not several.

### What the venue actually sends

Traced for order `2041946741522435`:

```
event            venue_ts            amount    amount_traded   amount_at_create
order_created    1787357128350000  0.10000000       0            0.10000000
TRADE            1787357128730000  0.05585426
order_changed    1787357128730000  0.04414574  0.05585426        0.10000000
order_changed    1787357128731000  0.02489468  0.01925106        0.10000000
order_changed    1787357128731000  0.02250668  0.00238800        0.10000000
order_deleted    1787357128732000           0  0.02250668        0.10000000
```

`live_orders` reports the **post-fill remaining quantity**. The `order_changed` at
`...128730000` already has the trade's 0.05585426 subtracted from it.

Measured over the whole capture:

- `amount_traded` is the fill for **that single event**, not a running total: `previous_amount -
  amount_traded == amount` holds for 156 of 156 filled events, while the cumulative reading
  (`amount_at_create - amount_traded == amount`) holds for only 111 — exactly the subset where the
  event happens to be the order's first fill.
- Every `order_changed` that decreased quantity in this capture was fill-driven: 83 with
  `amount_traded > 0`, 0 with `amount_traded == 0`.
- 168 of 170 fill-carrying order events match a trade on `(venue_timestamp, amount)`.

### Why the current design double-counts

At `...128730000` the order event and the trade share a timestamp. Order wins, so:

1. `apply(modify)` sets the book to 0.04414574 and `observe` records that quantity.
2. The trade is then processed. `reconcile` compares the trade quantity 0.05585426 against the
   recorded 0.04414574, finds `trade >= resting`, and emits a **remove**.
3. The order leaves a book where 0.04414574 should still rest.
4. The next `order_changed` at `...128731000` hits `unknown_order_id`.

The fill is subtracted twice: once by the venue's own post-fill quantity, once by the reconciler.

`Replay` already tolerates one shape of this. `correctionRemovals` lets a later raw `remove` for a
correction-removed id pass as `redundantOrderRemovals`. That tolerance covers `EventKind::remove`
only, so the three failing `modify` events are what actually abort the run. Widening the tolerance to
`modify` would hide the symptom while leaving the book quantity wrong, and is therefore not a fix.

### Why the tie-break alone cannot fix it

Neither ordering is correct for both shapes this capture contains:

- **`order_changed` at the same timestamp as its trade** — order-first double-counts, as above.
  Trade-first would handle it.
- **`order_created` at the same timestamp as a trade that consumes it** — trade-first means
  `reconcile` runs before `observe` has ever seen the order, finds nothing, and emits no correction;
  the `add` then applies and the order rests forever with no removal. Order-first prevents this.

Any fixed tie-break is wrong for one of them. The correction must stop depending on ordering.

## Options considered

1. **Trade-first tie-break** — one-line change; fixes the observed nine failures. Rejected: it
   reintroduces the never-removed-order case above, which is the failure `TradeReconciler` exists to
   prevent. Trades one silent corruption for another.

2. **Infer fills from quantity decreases inside `TradeReconciler`** — treat any `observe`d `modify`
   that lowers quantity as a fill already accounted for by `live_orders`, and correct only the
   shortfall. No signature or layout changes anywhere. Rejected: it assumes every decrease is a fill.
   That assumption holds in this capture (83 of 83) but is not guaranteed — a venue-side amend-down,
   or a resubscribe/reconnect where the first `order_changed` after a gap reflects fills that were
   never observed, both produce decreases that are not a single clean fill. The component whose only
   job is being right about fills should not be built on an inference the data cannot confirm.

3. **Carry `amount_traded` and correct only the shortfall** — the venue states the per-event fill
   explicitly, so a decrease driven by a fill is distinguishable from any other decrease, and a
   correction becomes idempotent: it applies only the part of a trade that `live_orders` has not
   already reported. Cost is plumbing (below), not ambiguity.

4. **Add `amount_traded` to `OrderEvent`** — rejected as the delivery mechanism for option 3.
   `OrderEvent` is 40 bytes and is embedded in `Record`, which
   `static_assert(sizeof(Record) == 56)` pins in `record.hpp`. An extra 8-byte field takes `Record`
   to 64 and forces a v3 on-disk format bump as a side effect of a bug fix. The book never reads
   `amount_traded`, so this would also violate the rule that keeps venue transport detail out of the
   normalized event — the same reasoning that already keeps `ChainLink` out of `OrderEvent`.

## Decision

Keep the order-wins-on-exact-tie rule, because `observe` must still precede `reconcile` at a shared
timestamp, and make trade corrections **idempotent** so that rule stops being load-bearing for
correctness.

`TradeReconciler` corrects only the **shortfall**: the part of a trade's quantity that `live_orders`
has not already reflected in the order's resting quantity. A trade whose fill the venue has already
reported produces no correction.

The per-event fill quantity reaches the reconciler through a **separate decode**, following the
`decodeChain` precedent, rather than as a new `OrderEvent` field. `OrderEvent`'s layout and Record v2
are unchanged.

## Consequences

- Corrections become order-independent: whether the trade or its `order_changed` is processed first,
  the resulting book quantity is the same. The tie-break remains as a determinism rule, not a
  correctness dependency.
- The nine `unknown_order_id` failures on the reference capture should reach zero without any
  hand-listed exceptions. This is the mechanism the three hardcoded adjustments in
  `test_golden_replay.cpp` were standing in for; those should be revisited once this lands.
- Cost: the fill quantity must travel decoder → `JoinedCapture` → `Replay` → `TradeReconciler`.
  `JoinedCapture` needs somewhere to hold it and `TradeReconciler::observe` needs to accept it.
  This is real work and touches four components.
- `Replay`'s `redundantOrderRemovals` tolerance stays, but should become rare rather than routine. If
  it stays common after this change, the shortfall logic is not working and the counter is the
  evidence.
- The two fill-carrying order events (of 170) that do not match a trade on `(timestamp, amount)` are
  unexplained. They should be traced before this ADR moves to accepted.
- Revisit if: a capture shows a quantity decrease with `amount_traded == 0`; a venue other than
  Bitstamp is added whose feed does not report post-fill quantities; or gap/reseed handling (still
  unspecified, plan v4 §7) changes what the reconciler can assume across a discontinuity.

## Open, deliberately not decided here

Gap, reconnect and reseed behaviour, the book-health state machine, and any reorder window are
required by plan v4 §7 but are not settled by this ADR. After a gap, quantity decreases may reflect
fills that were never observed, and no policy here covers that; it needs its own decision and its own
evidence.

## How you would defend this in an interview

I designed the merge with an exact-timestamp tie-break in favour of order events, reasoned from the
requirement that the reconciler's state be updated before it is read. Running it against a real
joined capture disproved that as sufficient: Bitstamp reports post-fill quantities on `live_orders`,
so applying the order event first caused the reconciler to subtract the same fill a second time and
remove nine live orders, and I could show that the opposite tie-break breaks a different case rather
than fixing it. The fix was to stop depending on ordering at all — carry the venue's own per-event
fill quantity and correct only the shortfall — and I kept that field out of the normalized event
because it would have forced an on-disk format change for data the order book never reads.
