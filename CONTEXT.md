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
Whether the engine currently permits a strategy decision after combining book trust, market shape,
operational state, and applicable risk policy.
_Avoid_: Market shape, book trust
