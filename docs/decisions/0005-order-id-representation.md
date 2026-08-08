# ADR 0005: Order ID representation

- **Status:** proposed
- **Date:** TODO
- **Slice:** 1

## Context

Coinbase issues UUID strings. Event structs must be trivially copyable and fixed size, so a string cannot live in one.

## Options considered

1. **Hash the UUID to uint64** — fast, fixed size, collisions are possible. What is the probability at your message rate, and what happens when one occurs?
2. **Intern into a dense integer table** — no collisions, needs a map lookup and a growing table.
3. **Store the raw 16 bytes** — exact, larger structs, comparisons are slower.

## Decision

TODO

## Consequences

TODO

## How you would defend this in an interview

TODO
