# ADR 0003: Error representation in the hot path

- **Status:** proposed
- **Date:** TODO
- **Slice:** 0

## Context

Follows from ADR 0002. If the hot path does not throw, submit() and try_pop() need a way to say no.

## Options considered

1. **std::optional** (learncpp 12.15) — no reason, just absence.
2. **Result<T, Error>** — carries a reason, more code to write.
3. **bool return plus out-parameter** — what the SPSC queue signature already implies.

## Decision

TODO

## Consequences

TODO

## How you would defend this in an interview

TODO
