# ADR 0002: Exceptions on the hot path

- **Status:** proposed
- **Date:** TODO
- **Slice:** 0

## Context

learncpp 27.8 and 27.9. Exceptions cost nothing when not thrown but they complicate the no-allocation guarantee and the noexcept contract on the SPSC queue. The network boundary genuinely has recoverable errors.

## Options considered

1. **Exceptions everywhere** — simplest, standard C++.
2. **Exceptions off entirely (-fno-exceptions)** — rules out parts of the standard library and most third-party libs.
3. **Exceptions allowed at the boundary, banned in the hot path** — mixed discipline, needs enforcement.

## Decision

TODO

## Consequences

TODO

## How you would defend this in an interview

TODO
