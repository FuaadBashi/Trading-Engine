# Paper Trading Engine

One C++/Python system in which the same strategy binary runs against recorded historical data
(backtest) and a live feed (paper trading), with only the data source and the clock swapped.

**The deliverable combines a validated engine and an interactive local dashboard.** The technical
evidence is a gap-aware L3 replay system plus an out-of-sample evaluation of fill-within-horizon
forecasts on real observed resting orders. The dashboard then exposes the same C++ engine for
historical replay, live observation and local paper-order experiments. See [the revised project
plan](docs/project-plan-v2.md) for the validation limits and schedule.

## Status

| Slice | Weeks | State |
|---|---|---|
| 0 Foundations | 0 | complete |
| 1 Data contract + recorder | 1 to 3 | in progress — capture, value types and normalized event storage complete; exact parser next |
| 2 Book builder | 3 to 5 | not started |
| 3 Replay engine v1 | 6 to 8 | not started |
| 4 Queue + execution model | 9 to 11 | not started |
| 5 Corpus validation | 12 to 14 | not started |
| 6 Operational live-data path | 15 to 16 | not started |
| 7 Interactive dashboard | after engine validation | not started |

## The one idea this repo is shaped around

Only three things vary between backtest and live:

| Interface | Replay | Live |
|---|---|---|
| `Feed` | `ReplayFeed` | `LiveFeed` |
| `Clock` | `EventClock` | `SystemClock` |
| `ExecutionVenue` | `SimulatedVenue` | `PaperVenue` (external sandbox adapter is stretch) |

If any file outside those six implementations calls the system clock or knows what a WebSocket
is, the design has leaked. CI enforces the clock half of that.

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

## Slice 1 order of attack

Never do these in parallel. Full detail in [docs/slice-1-plan.md](docs/slice-1-plan.md).

1. ~~Raw order JSON to snapshot-backed segments.~~ — capture and manifest validation work.
2. ~~C++ value types and normalized events, tests first.~~ — header contracts and behavior tests pass.
3. `InstrumentSpec` and exact decimal parsing, with no `double` conversion.
4. C++ decoder reading **that file**. No networking.
5. Explicit versioned binary writer + semantic/binary golden tests.
6. Add the joined Bitstamp order/trade capture contract required for fill labels.
7. Only now, swap the file reader for Boost.Beast.

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
