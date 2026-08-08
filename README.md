# Paper Trading Engine

One C++/Python system in which the same strategy binary runs against recorded historical data
(backtest) and a live feed (paper trading), with only the data source and the clock swapped.

**The deliverable is not a strategy.** It is a measured, quantitative comparison of predicted
versus actual fills: a data-backed answer to "why doesn't backtest Sharpe match live".

## Status

| Slice | Weeks | State |
|---|---|---|
| 0 Foundations | 0 | scaffolded, not built |
| 1 Recorder | 1 to 2 | not started |
| 2 Book builder | 3 to 5 | not started |
| 3 Replay engine v1 | 6 to 7 | not started |
| 4 Queue + latency model | 8 to 10 | not started |
| 5 Live path | 11 to 12 | not started |
| 6 Validation | 13 to 14 | not started |

## The one idea this repo is shaped around

Only three things vary between backtest and live:

| Interface | Replay | Live |
|---|---|---|
| `Feed` | `ReplayFeed` | `LiveFeed` |
| `Clock` | `EventClock` | `SystemClock` |
| `ExecutionVenue` | `SimulatedVenue` | `CoinbaseVenue` |

If any file outside those six implementations calls the system clock or knows what a WebSocket
is, the design has leaked. CI enforces the clock half of that.

## Layout

```
include/te/     public headers, namespace te
src/            implementations
apps/           thin main() files: recorder, replay, live
strategies/     Strategy implementations
bindings/       pybind11 module for research
python/         research + GUI
tests/          unit + golden
docs/decisions/ one ADR per real decision
scripts/        throwaway tooling (raw websocket dump lives here)
```

## Build

```
cmake -B build -DTE_SANITIZE=address,undefined
cmake --build build
ctest --test-dir build
```

## Slice 1 order of attack

Never do these in parallel:

1. `scripts/dump_raw_ws.py` — raw JSON to a file. Leave it running an hour.
2. C++ decoder reading **that file**. No networking.
3. Binary record writer + read-back round-trip test.
4. Only now, swap the file reader for Boost.Beast.

## Notes on measurement

Latency figures in this repo are **internal tick-to-order on macOS**, not end-to-end network
latency and not sub-microsecond claims. Methodology, hardware, and what is actually being
measured go in `docs/latency_methodology.md` before any number is quoted anywhere.
