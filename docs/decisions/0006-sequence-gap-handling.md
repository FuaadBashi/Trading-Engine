# ADR 0006: Behaviour on a sequence-number gap

- **Status:** proposed
- **Date:** TODO
- **Slice:** 1

## Context

The venue numbers messages. A gap means you have lost book state. Silently continuing produces a wrong book that looks fine.

## Options considered

1. **Reconnect and re-snapshot** — correct, loses a window of data, complicates the capture format.
2. **Mark a gap record in the capture and continue** — keeps the stream, makes downstream code handle discontinuity.
3. **Both: mark the gap AND re-snapshot** — most work, most correct.

## Decision

TODO

## Consequences

TODO

## How you would defend this in an interview

TODO
