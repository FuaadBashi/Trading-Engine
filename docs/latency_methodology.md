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
