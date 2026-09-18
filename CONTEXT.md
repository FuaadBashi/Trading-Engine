# Trading Engine Domain

This glossary separates market-data truth from visible market conditions and permission to trade.

## Language

**Book trust**:
Whether the reconstructed order book is based on a valid snapshot and an uninterrupted, successfully
processed event stream.
_Avoid_: Book usability

**Market shape**:
The visible relationship between the best bid and best ask: empty, one-sided, locked, crossed, or
open. It does not say whether the underlying data is trustworthy.
_Avoid_: Book health, trading permission

**Decision readiness**:
Whether the engine permits a strategy decision given book trust, market shape and applicable
operational permission. It does not mean a particular proposed order has passed admission.
_Avoid_: Market shape, book trust

**Order admission**:
Whether a particular order intention satisfies the limits required before it can be submitted,
including applicable outstanding exposure.
_Avoid_: Decision readiness, accepted fill

**Information availability**:
The time at which an observation may influence a strategy, distinct from when the venue says
the event occurred and when the strategy's resulting order arrives.
_Avoid_: Venue timestamp, network latency

**Aggregate depth agreement**:
Agreement on resting total quantity at each side and price. It does not establish agreement on
individual order identity or queue priority.
_Avoid_: Full L3 equivalence, proven FIFO

**Position basis**:
The assigned entry value of an open position under the selected accounting convention.
It must be interpreted together with that convention's treatment of fees and partial closes.
_Avoid_: Cash balance, market value

**Censoring**:
Incomplete observation of the target outcome over its required horizon, for example when usable
recording ends. An observed cancellation is a distinct event whose treatment depends on the target.
_Avoid_: Cancellation, failure to fill
