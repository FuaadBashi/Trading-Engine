# ADR 0007: Price level storage in the reference order book

- **Status:** accepted
- **Date:** 2026-08-16
- **Slice:** 2

## Context

Slice 2 needs a book that is complete, easy to reason about and trustworthy enough to act as the
correctness oracle for later optimized implementations. Constant-time price lookup is a possible
optimization, not a correctness requirement for this first book.

Two measured ranges rule out one flat array for the complete Bitstamp book:

- All 1,433 order events in the reference segment span `$58,356.10` to `$66,785.32`: `$8,429.22`
  or 842,922 one-cent ticks. This is event-price dispersion, not the bid-ask spread. The paired
  snapshot's best bid/ask are `$64,840.11` / `$64,840.12`, a one-cent spread.
- The `group=2` snapshot contains 4,174 bids and 4,563 asks at 6,511 distinct prices. Its extremes
  are a `$0.01` bid and a `$483,980,000.00` ask, about 48.4 billion possible one-cent ticks apart.
  A complete flat array would allocate almost entirely empty slots.

The book must support two independent searches:

1. price to price level, for adding orders and answering best-price/depth queries;
2. order ID to the exact resting order, for modifying and removing orders.

The public Bitstamp documentation says `group=2` supplies individual order IDs, but does not promise
that rows sharing a price are returned in matching-engine FIFO order. Storage must not silently turn
the received row order into a claim of known venue priority.

## Options considered

1. **Flat array indexed by tick offset** — fastest direct lookup and excellent locality, but needs a
   bounded price band and cannot represent the measured full snapshot without enormous waste.
2. **Hash map plus a separate ordered price index** — sparse and potentially fast, but two
   structures must remain synchronized before a trusted reference implementation exists.
3. **Sorted vector** — sparse and cache-friendly, but creating/removing a price level shifts
   elements and can invalidate locators.
4. **`std::map` for levels plus `std::unordered_map` for order IDs** — sparse, ordered and simple;
   tree nodes and per-node allocations cost locality, but the design has one source of truth for
   occupied prices and is easiest to validate.

## Decision

Build the first correctness reference book with:

- one ascending `std::map<Price, PriceLevel>` for buy levels;
- one ascending `std::map<Price, PriceLevel>` for sell levels;
- one `std::unordered_map<OrderId, OrderLocator>` for direct order lookup;
- an ownership-safe standard-library queue/list inside each `PriceLevel` before any custom pool or
  intrusive structure is introduced.

With ascending maps, the best bid is the final occupied buy level and the best ask is the first
occupied sell level. The reference book has no price band: every valid observed price is stored.

The order-ID index does not own orders. It locates an order owned by a price level. Removing the
final order at a price removes that empty price level as part of the same successful operation.

The snapshot's same-price row order may be preserved to make replay deterministic, but it is marked
as **priority unknown**. It must not be described as proven FIFO. Orders observed joining through
the live stream after the snapshot have known arrival order relative to later observed orders.

An optimized book is a later measured replacement, not part of this decision. Likely candidates
include a dense window around the active market with sparse overflow, a sorted vector, or a flat
hash/ordered-index design. Every candidate must replay identically to the reference book before it
can replace it.

## Consequences

- The `$0.01` bid and `$483,980,000.00` ask can coexist without allocating the prices between them.
- There is no out-of-band policy in the reference book because it has no band.
- The simplest implementation pays O(log L) price-level lookup and node-allocation/cache costs.
- Direct order lookup is average O(1), while best bid/ask and top-N traversal use the ordered maps.
- Correctness tests and debug invariants can target one ordered source of truth instead of keeping a
  hash map and second price index synchronized.
- Exact queue claims about orders already present in the starting snapshot are censored or labelled
  unknown. This does not prevent exact L1/L2 reconstruction. For a hypothetical order inserted
  after the snapshot, all quantity already resting at that price is known to be ahead even when the
  initial orders' internal order is unknown.
- Fill simulation additionally requires the synchronized `live_trades` stream and documented
  treatment of modifications, cancellations, hidden liquidity, gaps and modelled market impact.
- Performance is revisited only after correctness replay, same-venue comparison and benchmarks
  provide evidence.

## Future dense-window policy

If measurements later justify a dense near-market window:

1. An out-of-window order is stored in sparse overflow and counted; it is never silently dropped.
2. A distant order alone does not move the window.
3. The window is rebased only when the best bid/ask approaches a configured guard region near an
   edge.
4. Rebasing builds a replacement window, migrates eligible levels, validates it and then swaps it
   into use while retaining far orders in overflow.
5. If an order cannot be represented or rebasing fails, the book is marked invalid and the engine
   stops making decisions until it is reseeded.

## How you would defend this in an interview

The measured Bitstamp book occupied only 6,511 prices across a range of roughly 48.4 billion
one-cent ticks, so a complete flat array was not a credible first representation. I used ordered
sparse maps plus an order-ID index to build a simple correctness oracle, then required optimized
candidates to match it event-for-event. I also separated known live arrival order from unproven
same-price ordering in the initial REST snapshot rather than overstating queue accuracy.
