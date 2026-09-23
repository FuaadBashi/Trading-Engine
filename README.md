# L3 Replay and Execution-Research Engine

A deterministic C++ engine that rebuilds a venue's full order-by-order (L3) book from recorded
market data and replays it exactly. The goal is for one strategy to run against recorded data
(backtest) and a live feed (paper trading), with only the data source and clock swapped.

**What exists today:** Bitstamp L3 capture, merge and reconciliation, a portable tape format, and
a reference order book that reproduces the venue's own checkpoint exactly (0 of 4,533 levels
differ). **What doesn't yet:** the strategy, simulated venue, risk checks and live path. Portfolio
accounting is in progress. [Plan v4](docs/project-plan-v4.md) has the full scope and gates.

## Status

Stage 5 (replay and accounting) is in progress. `Portfolio::applyFill` handles opening a long;
closing, shorts and reversals are next, driven by 16 specification tests enabled one at a time.
[TODO.md](TODO.md) owns live task status; [status](docs/handoff/status.md) is the session handoff.

| Stage | Focus | State |
|---|---|---|
| 0-3 | Contracts, joined capture, merge/reconciliation, golden correctness | done; hardening open (TODO C) |
| 4 | Portable v3 tape | format done; raw/tape equivalence open |
| 5 | Replay and accounting | **in progress** (Portfolio) |
| 6 | Queue labels and baselines | not started |
| 7 | Held-out validation | not started |
| 8 | Performance laboratory | planned (TODO S8.1-S8.7) |
| 9 | Operational paper path | not started |
| 10 | Dashboard | not started |

## The one idea this repo is shaped around

Only three things vary between backtest and live:

| Interface | Replay | Live |
|---|---|---|
| `Feed` | `ReplayFeed` | `LiveFeed` |
| `Clock` | `EventClock` | `SystemClock` |
| `ExecutionVenue` | `SimulatedVenue` | `PaperVenue` (external sandbox adapter is stretch) |

If any file outside those six implementations calls the system clock or knows what a WebSocket
is, the design has leaked. The injected clock boundary exists, and CI now enforces direct-clock and
selected financial-header guards with focused deliberately-broken tests. Financial implementation
files need explicit guard coverage as Portfolio/risk are added; the current guard is not comprehensive.

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

The live sequence and its exact completion gates are in
[TODO.md](TODO.md); long-range reasoning remains in [Project Plan v4](docs/project-plan-v4.md).

The guide explains the learning path; TODO owns priorities and completion criteria. Superseded
plans and PDF exports are in [docs/archive](docs/archive/README.md).

Stage 8's [low-latency sequence](TODO.md#stage-8-sequence-low-latency-evidence) follows the
complete Stage 5 engine and experiment runner: define timed paths, baseline, profile, compare
one book variant and queue designs, audit measurement bias, then publish correctness and
performance evidence. These are in-process measurements, not exchange round-trip latency.
No latency or speedup result is claimed until measured; see [the measurement contract](docs/project-plan-v4.md#18-stage-8---c-performance-laboratory).

Done before this list: the mandatory committed golden fixture; timestamp and ID contract fixes;
portable v3 encoding/segment I/O; CI architecture guards; joined order/trade capture under one
manifest and ordinal; the
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

No current engine latency result is established by this documentation update. Distinguish
internal processing time from simulated availability and receipt-minus-venue clock differences.
Methodology, hardware, sample size and uncertainty go in `docs/latency_methodology.md` before
making a performance claim. The reference book remains the oracle; advanced C++ learning
variants enter the main engine only after correctness and measurement justify them.
