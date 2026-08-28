# ADR 0013: Merge ordering and fill double-counting in the replay controller

- **Status:** accepted
- **Date:** 2026-08-27
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

## Correction to the first draft of this design

The first version of the shortfall rule kept **one running credit per order**, with no timestamp.
That is wrong, and the counter-example is worth recording because the flaw is not obvious.

Take a trade-only fill (no order event ever arrives) followed by an ordinary paired fill later:

```
start                              book 10   credit  0
trade-only fill of 6 at T1         book  4   credit -6      (correction fires, correctly)
order_changed at T2, traded 1      book  3   credit -5
matching trade of 1 at T2                    credit is negative, so a correction fires again
```

The correct book quantity is 3. The single running credit produces less, because a debt left over
from T1 contaminated an unrelated fill at T2. The positive direction fails the same way: a credit
of +6 whose trade never arrives silently swallows a later genuine fill of 1.

The fix is to scope each credit to a **fill correlation key**:

```
(orderId, venueTimestampMicros)
```

Credits at different timestamps are isolated and can never consume each other. Within one timestamp
the credit is a fungible pot, which is required: 10 orders in the reference capture carry more than
one fill at the same microsecond.

The capture supports this key directly: 144 of 146 `(orderId, timestamp)` buckets balance to exactly
zero between the two streams, and **zero** fills have a matching trade at a *different* timestamp.
The two that do not balance share timestamp `1787357166111000` and the same quantity — the two sides
of one trade whose message fell outside the capture window.

## Consequences

Measured on `data/raw/bitstamp-btcusd-20260822T000512Z`, locked in by
`BitstampJoinedCapture.RealCaptureReplaysToCheckpointWithNoResiduals`:

| | Before | After |
|---|---|---|
| replay outcome | `unexpected_order_apply_failure` | completes |
| `unknown_order_id` failures | 9 | **0** |
| checkpoint level mismatches | n/a (aborted) | **0 of 4533** |
| `redundantOrderRemovals` | — | 0 |
| corrections generated | — | 0 |

- Corrections are order-independent: whether the trade or its `order_changed` is processed first,
  the resulting book quantity is the same. The tie-break is now a determinism rule, not a
  correctness dependency.
- The replayed book reproduces the independent S1 snapshot exactly, with no hand-listed exceptions.
- **This capture does not exercise the correction path** — and, on the evidence below, no Bitstamp
  capture may. See "The correction path has not been reached on real data".
- Cost, as predicted: the fill travels decoder → `JoinedCapture` → `Replay` → `TradeReconciler`,
  touching four components. `decodeFill` is a separate decode following the `decodeChain` precedent,
  so `OrderEvent`'s layout and Record v2 are unchanged.

### The correction path has not been reached on real data

`correctionsGenerated` is 0 on every joined capture taken so far. That is not an accident of a short
sample. A correction fires only when a trade's quantity is **not** already covered by an
`amount_traded` credit at the same `(orderId, venueTimestampMicros)`. Measured across three segments,
1,059 seconds and 753 trades:

| Segment | Span | Trades | Naming a resting order | Covered by a credit | Uncovered |
|---|---|---|---|---|---|
| `20260822T000512Z` seg 0 | 65s | 84 | 69 | 69 | **0** |
| `20260827T235604Z` seg 0 | 856s | 582 | 530 | 530 | **0** |
| `20260827T235604Z` seg 1 | 138s | 87 | 38 | 38 | **0** |
| **total** | **1,059s** | **753** | **637** | **637** | **0** |

**637 of 637.** Bitstamp's `live_orders` reported every fill against a resting order via
`amount_traded`. Not usually — every one.

This weakens a claim made earlier in this project. The three hardcoded adjustments in
`test_golden_replay.cpp` were explained as orders "fully filled, and `live_orders` emits no delete for
a fill." If `live_orders` reports every fill, that explanation is unlikely. That capture is order-only
so the hypothesis cannot be tested directly against it, and those three remain unexplained.

Consequences for the reconciler:

- It is **insurance, not a load-bearing path**, on this venue's L3 feed as observed. Keep it: it is
  correct, cheap, unit-tested, and other venues do not all report post-fill quantities. But do not
  claim it is validated against live venue data, because it has never fired against any.
- **Stop capturing in the hope of finding the case.** Three captures produced zero reachable
  instances. Further captures should be justified by something other than this search.
- A future venue whose feed omits fill reporting would exercise it immediately, and is the more
  likely source of real evidence than a longer Bitstamp run.

### A capture-side prerequisite this work uncovered

Replaying the second capture failed with `unexpected_order_apply_failure` — seven removes for orders
the book had never seen. Cause: after a chain-gap reseed, the served snapshot's microtimestamp
(`1787875831682714`) **predated the segment's first captured event** (`1787875832061000`) by 378ms.
Events in that window are covered by neither the snapshot nor the stream.

Subscribing before fetching the snapshot — which the capture script already did — is necessary but
not sufficient, because Bitstamp can serve a snapshot describing the book as it was several hundred
milliseconds earlier. The capture script now refetches until
`snapshot.microtimestamp >= first captured order event`, and ends the segment with
`snapshot_never_overlapped_stream` rather than emitting a seed with a hole in it. This is the only
provable coverage condition; anything weaker is a probabilistic bet on an empty window.

Related: the manifest previously advertised a `checkpoint` path for segments that ended on a chain gap
and never wrote one, which made `loadJoinedCapture` fail on the whole capture. File paths are now
recorded only when the file exists.

### The health counter had to be redefined

The first counter, `ordersRemovedWithOpenFillBalance`, read **21** on a capture with zero residuals —
so it was not measuring a fault. All 21 are orders whose fill-carrying `order_changed` shares a
timestamp with its `order_deleted`. Because order events win exact ties, every order event at time T
is processed before any trade at T, so the delete always erases the credit before the matching trade
can clear it. That is structural and unavoidable under this tie-break.

Renamed to `ordersRemovedWithUnmatchedFill` and restricted to credits from an **earlier** timestamp,
where a trade genuinely never arrived while the order was live. It now reads **0**, alongside
`staleFillsDiscarded` at **0**. Both are meaningful signals again: non-zero means one stream reported
a fill the other never confirmed.

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
