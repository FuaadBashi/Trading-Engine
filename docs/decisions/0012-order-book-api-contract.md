# ADR 0012: Reference order-book API and failure contract

- **Status:** accepted
- **Date:** 2026-08-16
- **Slice:** 2

## Context

The current `OrderBook` sketch returns a `Price` for best bid/ask even when a side may be empty, and
`apply` returns `void` even though duplicate IDs, unknown IDs and invalid event values need different
recovery and telemetry. Sentinel prices and silently ignored events would allow a wrong book to
look healthy.

ADR 0003 already chooses `std::optional` for reasonless absence and `Result<T, E>` when callers need
the failure reason.

## Options considered

1. **Sentinel values and `void apply`** — smallest interface, but confuses absence with real data and
   hides corruption.
2. **Exceptions** — expressive, but inconsistent with the hot-path policy and harder to count and
   recover from deterministically.
3. **Optional best prices and a reasoned apply result** — explicit, testable and consistent with ADR
   0003.

## Decision

- `bestBid()` and `bestAsk()` return `std::optional<Price>`. An empty optional means that side has no
  resting order. No numeric price is reserved as a sentinel.
- `qtyAt(side, price)` returns zero quantity when no level exists. For this aggregate query,
  “missing level” and “zero resting quantity” have the same useful meaning.
- `apply(event)` returns `Result<ApplyOutcome, ApplyError>` and never silently ignores an invalid
  event.
- `ApplyOutcome` records whether applying the event created or removed a price level, providing
  useful telemetry without requiring the caller to inspect internal containers.
- `ApplyError` distinguishes at least: duplicate order ID, unknown order ID, invalid price, invalid
  quantity, side mismatch and level-quantity overflow.
- Internal invariant failure is a programming/system failure, not bad market input. Debug builds
  assert immediately; a production controller marks the book unhealthy and stops decisions.

Event rules:

- **Add:** price and quantity must be positive and the ID must not already exist.
- **Modify:** the ID must exist and the side must agree; the new price and remaining quantity must
  be positive. A changed price moves the order from its stored old level to the reported new level.
  The hour capture contains 72 such same-ID price moves, so treating price as immutable would reject
  valid venue data. Queue-priority effects remain governed by ADR 0008 once Bitstamp semantics are
  proven.
- **Remove:** the ID must exist and the side must agree. Removal locates the order by ID and erases
  its **stored** price and remaining quantity. The deletion message's price and quantity are audit
  or execution/replacement data, not the resting identity to subtract. This is required by the
  reference capture: nine valid deletes carry a price different from the order's stored resting
  price, including fully traded orders whose delete carries the execution price and zero amount.

The book remains unaware of files, sockets, manifests and gaps. A replay/live controller owns the
book-validity state. An unknown ID is always reported by the book; only the controller has enough
context to classify it as startup-boundary evidence, corrupt input or a reason to reseed.

Method names use the project's lower-camel convention: `bestBid`, `bestAsk`, `qtyAt`.

## Consequences

- Empty books are represented honestly without inventing a price.
- Every failed update can be counted, logged and connected to a recovery policy.
- Callers must handle results explicitly, which adds code but prevents silent state drift.
- Unit tests can cover each event rule before implementation.
- `Result<ApplyOutcome, ApplyError>` works with the existing generic `Result` without first needing a
  `Result<void, E>` specialization.
- The public API does not expose storage containers, so the implementation can later change from
  `std::map` to a measured optimized layout.

## How you would defend this in an interview

An empty side of a book is absence, not price zero, so best-price queries return optional values.
Applying market data has several recoverable failure reasons, so it returns a reasoned result and
never silently absorbs unknown or contradictory events. Gap and reseed policy stays in the driver,
keeping the book deterministic and independently testable.
