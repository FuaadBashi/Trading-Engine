# ADR 0007: Price level storage in the order book

- **Status:** proposed
- **Date:** TODO
- **Slice:** 2

## Context

Levels have to be found in O(1) on every message. Crypto prices move a long way over a session.

## Options considered

1. **Flat array indexed by tick offset from a base** — fastest, fails when price leaves the band. What is the band, and what happens on exit?
2. **Hash map keyed by price** — no band problem, worse cache behaviour.
3. **Sorted vector** — good locality, insertion cost in the middle.

## Decision

TODO

## Consequences

TODO

## How you would defend this in an interview

TODO
