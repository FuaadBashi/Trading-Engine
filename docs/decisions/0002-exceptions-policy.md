# ADR 0002: Exceptions on the hot path

- **Status:** accepted
- **Date:** 2026-08-08
- **Slice:** 0

## Context

learncpp 27.8 and 27.9. Exceptions cost nothing when not thrown but they complicate the no-allocation guarantee and the noexcept contract on the SPSC queue. The network boundary genuinely has recoverable errors.

## Options considered

1. **Exceptions everywhere** — simplest, standard C++.
2. **Exceptions off entirely (-fno-exceptions)** — rules out parts of the standard library and most third-party libs.
3. **Exceptions allowed at the boundary, banned in the hot path** — mixed discipline, needs enforcement.

## Decision

Permit exceptions at cold system boundaries, including startup, configuration, file
opening, network connection setup and third-party integration. Exceptions must not
escape latency-sensitive hot-path operations such as applying market events, updating
the order book, queue push/pop operations, strategy callbacks and per-event risk
calculation.

Hot-path functions use explicit status returns and are marked `noexcept` when they can
truthfully provide a no-throw contract. Do not compile the entire project with
`-fno-exceptions`, because standard-library and third-party code may rely on exception
support.

## Consequences

- Cold-path failures can carry detailed diagnostic context and integrate naturally
  with third-party APIs.
- Hot-path latency does not include the unpredictable cost of throwing and unwinding.
- Hot-path failure is visible in function signatures and must be handled explicitly.
- The mixed policy requires discipline and code review; `noexcept` is a contract, not
  a mechanism that prevents a throw.
- If an exception escapes a `noexcept` function, `std::terminate` is called.
- Exceptions and their supporting metadata remain enabled in the overall binary.

## How you would defend this in an interview

Exceptions are useful at cold boundaries where failures are uncommon and descriptive
context matters. The event-processing hot path instead uses explicit status returns
and honest `noexcept` contracts so failure behavior and latency remain predictable. A
global `-fno-exceptions` policy would unnecessarily constrain the standard library and
third-party dependencies.
