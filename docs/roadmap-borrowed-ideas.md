# Borrowed ideas: what to take from real engines, and where

Eight designs from production systems, each placed in an existing TODO stage. This changes how
D4-D6 and E1 are built, not what they must prove. Fuaad writes the code; the assistant writes the
tests from agreed examples.

| # | Idea | From | Stage | Size |
|---|---|---|---|---|
| 1 | SPSC ring buffer, single writer | LMAX Disruptor, Rigtorp `SPSCQueue` | **now**, beside D2 | 3-5 days |
| 2 | Order lifecycle as an explicit state machine | FIX 4.4 execution reports, QuickFIX | D4 | 2-3 days |
| 3 | Fixed pipeline order: risk, then match, then settle | exchange-core | D4 | 1 day |
| 4 | A small matching core against `OrderBook` | Liquibook | D4 | 3-4 days |
| 5 | Fill, fee and latency as swappable models | QuantConnect LEAN | D4-D5 | 1-2 days |
| 6 | Two latencies: feed and order entry | hftbacktest | D5 | 1-2 days |
| 7 | Same strategy code in replay and live | NautilusTrader | D5-D6 | shapes design |
| 8 | Config-driven parameter sweeps | Hummingbot | E1 | 1-2 days |

## 1. SPSC ring buffer — pulled forward

Independent of accounting, so it can be built while D2 continues. A standard interview question.

- Power-of-two capacity, head and tail on separate cache lines (`alignas(64)`), acquire/release
  ordering only, each side caching the other's index. `tryPush`/`tryPop` never block.
- **Single-writer principle:** each index has exactly one thread that writes it. That is the
  reason no lock or `seq_cst` is needed. Be able to say why.
- **Done when:** a TSan stress test (two threads, millions of items, order and count checked)
  passes, and a benchmark against a `std::mutex` + `std::deque` queue reports p50/p99/p99.9.
- **Needs first:** a TSan build (`-DTE_SANITIZE=thread`) and Google Benchmark enabled in
  `cmake/dependencies.cmake` (already stubbed there).
- Not wired into the engine until Stage 8. D5 stays single-threaded.

## 2. Order lifecycle state machine

Model the venue's view of your order the way FIX execution reports do.

```
PendingNew -> New -> PartiallyFilled -> Filled
      |        |           |
      v        v           v
  Rejected  PendingCancel -> Cancelled
```

- One pure function: `(state, event) -> Result<new state, TransitionError>`. No I/O, no venue.
- Illegal transitions are errors, never ignored. A fill arriving in `PendingCancel` is **legal**:
  that is the cancel race D4 already requires.
- **Done when:** a table test covers every (state, event) pair, legal and illegal.
- **Decide:** whether `Replaced` (amend) exists in Stage 5. Recommendation: no.

## 3. Risk, then match, then settle

exchange-core never lets an order reach matching before risk has accepted it, and never
updates balances until matching has produced a fill. Adopt the same fixed order in D4:

```
intent -> pre-trade risk -> venue accept -> match -> fill -> Portfolio::applyFill
```

- Risk is a pure function of (order, portfolio snapshot, outstanding orders). A rejection
  cannot touch venue or portfolio state; D4 already requires this.
- Minimum checks: order size, notional, resulting absolute position including outstanding
  orders, price collar. Rate limits wait for Stage 9.

## 4. Matching core

The book is only replayed today; nothing matches against it. A ~200-line matcher crosses your
simulated aggressive order against the resting book, best price first, then time priority.

- Read Liquibook's matching loop before writing yours.
- **Done when:** hand-worked cases pass: full fill at one level, walk through three levels,
  partial fill with remainder resting, and no fill when not marketable.
- **Decide before Stage 6:** does increasing an order's size lose queue position? Most venues
  (CME, Nasdaq) say yes. `OrderBook` currently keeps position. Measure Bitstamp from the L3
  captures, then record it as a rule the queue model reads.

## 5. Swappable fill, fee and latency models

LEAN separates *what* fills from *how* fills are priced. Give the simulated venue three small
interfaces instead of hard-coded policy:

- `FillModel` — when and how much fills (start: fill at touch; later: queue-aware, Stage 6).
- `FeeModel` — computes the fee `Portfolio` receives (flat, then basis points, then maker/taker).
  This also settles ADR 0014 D5's one open item, the fee schedule.
- `LatencyModel` — see 6.

Start with one implementation each. Settle ADR 0009 (runtime vs static dispatch) here.

## 6. Two latencies

hftbacktest models two delays separately: when you *see* market data (feed latency) and when
your order *reaches* the venue (order-entry latency). D5's availability policies (zero, fixed
80 ms, recorded) are the feed half. Add the order-entry half so fill eligibility starts at
`submit time + entry latency` (ADR 0014 D3), not at submit time.

- **Done when:** a timeline test shows an order cannot fill against a level that was removed
  before it arrived, even though it was visible when submitted.

## 7. Same strategy code in replay and live

NautilusTrader runs one engine loop; only the data and execution clients change. This repo
already states the idea (README, "three things vary") but has no code enforcing it.

- The engine loop takes `Feed`, `Clock` and `ExecutionVenue` by injection and never names a
  concrete type.
- **Done when (D6):** a test runs the same strategy through two fake feeds with identical
  events and gets identical decisions and account state.

## 8. Config-driven parameter sweeps

Hummingbot strategies are data: a config file, not code edits. For E1:

- `replay_main --config run.toml` (or JSON). Strategy parameters, fee model, latency policy and
  data selection all live in config.
- A Python sweep runner expands a grid, runs each, and writes one results table with each
  run's manifest hash.

## Deferred

- **Journal and snapshot recovery** (exchange-core): Stage 9, with the fill journal from D2.
- **Counterfactual market impact** (ABIDES): research extension only.
- **Fuzzing the decoder** (libFuzzer): after Stage 5; needs Homebrew LLVM on macOS.
- **Per-module CMake targets** so an illegal include fails to link rather than relying on the
  guard script: worth doing when D3 adds the first new module.
