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
assumption that inserting our order would not alter later behavior. The current order-only corpus
does not yet prove all of those semantics; a joined `live_orders` + `live_trades` sample is required
before this ADR can be accepted.

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
