# Latency methodology

No latency number appears in the README, in a CV, or in an interview until this file says
exactly what was measured, on what, and how.

TODO(fuaad), fill in before quoting any number:

- **What is measured** — which two points in the code, named precisely.
- **Clock used** — and its resolution.
- **Hardware and OS** — model, core count, what else was running.
- **Build** — compiler, version, optimisation flags, sanitisers off.
- **Sample size and distribution** — median, p99, p99.9. Never a bare mean.
- **What is NOT measured** — network, venue-side, kernel scheduling.

## Interpretation rules added 18 September 2026

- Record exact measurement endpoints; internal tick-to-order is not venue round-trip latency.
- Receipt wall time minus venue time includes unknown clock offset and possibly drift/adjustments.
  Positive differences do not establish synchronization; the observed median is not calibrated delay.
- ADR 0014's fixed 80 ms is a simulation scenario, not a performance measurement or safety bound.
- Report availability policy separately from the reconstruction ordering key and outbound delay.
- Compare optimized/reference runs only after intermediate order-level and known-priority equivalence.
- Use controlled hardware, warm-up, repeated runs, raw outputs, sample counts and uncertainty.
  Shared CI runners do not enforce nanosecond thresholds. Sanitizers belong in correctness runs,
  not the timed performance comparison.
