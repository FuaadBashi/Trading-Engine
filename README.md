# Paper Trading Engine

One C++/Python system in which the same strategy binary runs against recorded historical data
(backtest) and a live feed (paper trading), with only the data source and the clock swapped.

**The deliverable combines a validated engine and an interactive local dashboard.** The technical
evidence is a deterministic, gap-aware L3 order/trade replay system, a reference-versus-optimized
C++ performance study, and an out-of-sample evaluation of fill-within-horizon forecasts on real
observed resting orders. The dashboard exposes the same C++ engine for historical replay, live
observation and local paper-order experiments. See the current
[Project Plan v4](docs/project-plan-v4.md) for the evidence gates, limitations and schedule.

## Status

| Slice | Weeks | State |
|---|---|---|
| 0 Foundations | 0 | complete |
| 1 Data contract + recorder | 1 to 3 | substantially implemented; durable v3 and two contract fixes remain |
| 2 L3 book + reconciliation | 3 to 5 | merge controller built and its gate met; joined replay reproduces the venue checkpoint exactly (0 of 4,533 levels differ) |
| 3 Deterministic replay + accounting | after joined replay | not started |
| 4 Queue labels + execution model | after replay core | not started |
| 5 Held-out corpus validation | after label-quality gate | not started |
| 6 Performance laboratory | after deterministic correctness | not started |
| 7 Operational live/paper path | after validated core | not started |
| 8 Interactive dashboard | after stable telemetry contract | not started |

## The one idea this repo is shaped around

Only three things vary between backtest and live:

| Interface | Replay | Live |
|---|---|---|
| `Feed` | `ReplayFeed` | `LiveFeed` |
| `Clock` | `EventClock` | `SystemClock` |
| `ExecutionVenue` | `SimulatedVenue` | `PaperVenue` (external sandbox adapter is stretch) |

If any file outside those six implementations calls the system clock or knows what a WebSocket
is, the design has leaked. The injected clock boundary exists; the automated CI source guard is
still an explicit Plan v4 Stage 0 action and is not yet claimed as enforced.

## Layout

```
include/te/     public headers, namespace te
src/            implementations
apps/           thin main() files: recorder, replay, live
strategies/     Strategy implementations
bindings/       pybind11 module for research
python/         research + local web dashboard
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

## Venue

Primary is **Bitstamp `live_orders`** — genuine order-by-order L3, public, no authentication.
Coinbase Exchange's `full`/`level3` channels are institutional-gated and unreachable from a retail
account, which invalidated the original plan's venue premise. See
[ADR 0010](docs/decisions/0010-venue-selection.md).

Exact book-reconstruction checks must use Bitstamp snapshot/L2 data from the same venue. Coinbase
`level2_batch` is kept as an unauthenticated secondary adapter and cross-venue sanity signal; it
cannot exactly verify a Bitstamp book.

| | Script | Auth | Depth |
|---|---|---|---|
| Primary | `scripts/dump_raw_ws_bitstamp.py` | none | L3, order-by-order |
| Secondary | `scripts/dump_raw_ws.py` | none | L2, price-aggregated |

## Current order of attack

Never do these in parallel. Full detail and exit gates are in
[Project Plan v4](docs/project-plan-v4.md).

1. **Make a small end-to-end golden fixture mandatory on a clean checkout.** Today a fresh
   checkout reports 194/194 green while silently skipping every real-corpus test, including the
   correctness gate, so green does not yet mean much.
2. Correct the timestamp-unit contract and activate truthful CI guards.
3. Define replay-side gap/reseed behaviour and book health states, then gate strategy delivery on
   them. Deliberately deferred by ADR 0013 until there is evidence to design against.
4. Implement portable v3 encoding after the joined schema and timing fields are stable.
5. Only then begin the general replay/strategy/accounting engine.

Done since this list was written: joined order/trade capture under one manifest and ordinal; the
deterministic merge/reconciliation controller (ADR 0013); `id`/`id_str` agreement; and manual
checkpoint adjustments replaced by joined trade evidence with no silent apply errors. The three
remaining adjustments live only in the legacy order-only golden test, which has no trade stream to
reconcile against.

Each Bitstamp run creates `data/raw/bitstamp-btcusd-<UTC timestamp>/` containing an
atomic `manifest.json` plus one `.snapshot` and payload-only `.jsonl` file per continuous
segment. A gap, transport closure or `bts:request_reconnect` closes the segment; capture
resumes only after reconnecting and acquiring a fresh snapshot.

```
python scripts/dump_raw_ws_bitstamp.py 3600
python scripts/validate_capture.py data/raw/bitstamp-btcusd-<UTC timestamp>
python scripts/audit_book_bootstrap.py data/raw/bitstamp-btcusd-<UTC timestamp>
```

The older `data/raw/btcusd-live-orders.jsonl` remains a useful fault corpus: its completed
hour contains a real chain break. It must not be treated as one replayable continuous book.

## Notes on measurement

Latency figures in this repo are **internal tick-to-order on macOS**, not end-to-end network
latency and not sub-microsecond claims. Methodology, hardware, and what is actually being
measured go in `docs/latency_methodology.md` before any number is quoted anywhere.
