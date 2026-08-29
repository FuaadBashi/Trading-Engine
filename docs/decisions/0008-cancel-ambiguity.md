# ADR 0008: Queue-position assumption for cancels

- **Status:** proposed
- **Date:** TODO
- **Slice:** 4

## Context

The original premise was written for an L2 feed: when only aggregate level quantity is visible,
you cannot observe whether a cancellation at your price was ahead of or behind a hypothetical
order. Fill-rate estimates then depend strongly on an allocation assumption.

ADR 0010 changed the primary source to Bitstamp L3. A valid `group=2` snapshot plus individual
order IDs can locate each visible existing order relative to an insertion point. If a known order
later cancels, its ahead/behind location is observable. The old premise is therefore not universal
and must not be applied blindly to the L3 path.

Uncertainty has not disappeared. It moved to classifying deletion versus execution, replacement
and priority semantics, tied timestamps, hidden liquidity, capture gaps and the counterfactual
assumption that inserting our order would not alter later behavior.

### What joined capture has now settled (2026-08-28)

Joined `live_orders` + `live_trades` samples exist, and part of the question above is answered.

**Deletion versus execution is directly observable, not inferred.** Every order event carries
`amount_traded`, the fill for that single event, and it is non-zero exactly when the event was
caused by a trade. Across 1,059 seconds of joined capture, **637 of 637** trades against a resting
order had their fill reported this way, and **83 of 83** quantity decreases were fill-driven. A
delete with `amount_traded == 0` is a genuine cancel; one with a non-zero value is the final fill.
No allocation assumption is needed to tell them apart.

Two things this does **not** settle, and they are the reasons this ADR is still `proposed`:

- **Tied timestamps.** Order and trade messages share a venue microsecond routinely, and 10 orders
  in one capture carried more than one fill in the same microsecond. Within a microsecond the
  ordering of fills against a level is not recoverable from the feed.
- **The counterfactual itself.** Observability of *other* orders' cancels says nothing about
  whether inserting our own order would have changed later behaviour. That is a modelling
  assumption, and no capture can discharge it.

See ADR 0013 for the fill-accounting mechanism and the measurements above.

## Options considered

1. **All cancels behind you** — optimistic, overstates fill rate.
2. **All cancels ahead of you** — pessimistic.
3. **Uniform across the queue** — reduce queue-ahead by qty * (ahead / total).
4. **Weighted toward the back** — front-of-queue orders are older and less likely to be cancelled.

Note: the plan says implement at least two and report the spread. The spread is a headline result, not a footnote.

For the L3 path, add these options after the joined sample is analyzed:

5. **Exact visible-queue replay** - track each observed order ID and classify a cancellation by
   its actual location relative to the insertion point. Still conditional on displayed liquidity,
   event integrity and no market impact.
6. **Observed-order outcome forecasting** - avoid inserting a hypothetical order; predict the
   later fill/cancel outcome of a real `order_created` event using only information available when
   it arrived.

## Decision

TODO after order/trade joining demonstrates Bitstamp fill, cancellation and replacement semantics.

## Consequences

TODO. Consequences must distinguish exact visible L3 tracking from L2 sensitivity assumptions and
must name hidden-liquidity, censoring and no-market-impact limitations.

## How you would defend this in an interview

TODO. The defensible answer must explain why the original L2 cancellation ambiguity changed after
the venue/data upgrade, rather than repeating a standard queue-model assumption unchanged.
