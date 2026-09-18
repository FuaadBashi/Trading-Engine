# ADR 0009: Virtual dispatch vs CRTP for Strategy

- **Status:** proposed
- **Date:** TODO
- **Slice:** 3

## Context

One indirect call per market event on the strategy callback. Templates would remove it at the cost of compile-time coupling.

## Options considered

1. **Virtual functions** — runtime plugging, one vtable lookup per event, trivially testable.
2. **CRTP / static polymorphism** — no indirection, strategy type baked into the binary.
3. **std::function** — type-erased runtime callable; allocation depends on the callable and
   implementation. Compare its actual dispatch/allocation cost with virtual and static alternatives.

Correction, 2026-09-18: the previous blanket "worst performance, allocates" claim was unsupported.
Choose the simplest useful seam first; any performance claim needs a representative benchmark.

## Decision

TODO

## Consequences

TODO

## How you would defend this in an interview

TODO
