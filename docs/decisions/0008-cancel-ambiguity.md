# ADR 0008: Queue-position assumption for cancels

- **Status:** proposed
- **Date:** TODO
- **Slice:** 4

## Context

You cannot observe whether a cancel at your price level was ahead of or behind your order. Your fill-rate estimate depends entirely on this assumption. This is the single most important modelling decision in the project.

## Options considered

1. **All cancels behind you** — optimistic, overstates fill rate.
2. **All cancels ahead of you** — pessimistic.
3. **Uniform across the queue** — reduce queue-ahead by qty * (ahead / total).
4. **Weighted toward the back** — front-of-queue orders are older and less likely to be cancelled.

Note: the plan says implement at least two and report the spread. The spread is a headline result, not a footnote.

## Decision

TODO

## Consequences

TODO

## How you would defend this in an interview

TODO
