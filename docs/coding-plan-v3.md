# Paper Trading Engine — Coding Plan v3

**Owner:** Fuaad Bashi
**Revised:** 2026-08-15
**Supersedes:** `Trading_Engine_Coding_Plan.pdf` (August 2026)
**Related:** `docs/project-plan-v2.md` (deliverable and validation methodology). Where v2 and this
document disagree on *what is being proven*, v2 wins. Where they disagree on *what is being built*,
this document wins.

Self-directed build. The original plan's core rule stands: skeletons and invariants are given,
implementations are not. This revision exists because eight weeks of real building invalidated a
number of the original plan's assumptions, and because a plan that no longer matches the repo is
worse than no plan.

---

## 0. Rules of engagement

Unchanged in substance. You are writing this codebase; that constraint is the point.

1. **Allowed from AI:** interface critique after you have written it, error decoding, "why is this
   UB", resource pointers, review of a design you already committed to, and — added by experience —
   *empirical verification*: "run this and show me what it actually does."
2. **Not allowed:** function bodies and algorithm implementations for anything on the learning path.
3. **The test:** after any slice, close the editor and re-derive the core function on paper.
4. Every code block below is a **declaration or an invariant statement**. Bodies are the project.

**One rule added from experience.** Build-system plumbing, documentation reformatting and tooling
configuration are explicitly *not* the learning path. Delegating those is correct; delegating a
bounds check is not. The distinction is whether writing it teaches you something you must be able
to defend in an interview.

**One habit that has already paid for itself.** Every claim about behaviour gets verified by running
it, not by reading it. Several bugs in this repo compiled cleanly, passed review by eye, and were
only caught by executing them under UBSan or by printing the actual value. "It looks right" is not
evidence.

---

## 1. Where the project actually is

| Slice | Weeks | Status | Ships |
|---|---|---|---|
| 0 | 0 | **Done** | Build system, GoogleTest, CTest, sanitisers, CI-ready |
| 1 | 1–2 | **~80%** | Recorder: venue JSON to binary capture on disk |
| 2 | 3–5 | Not started | Book builder, verified against an independent depth source |
| 3 | 6–7 | Not started | Replay engine v1, L1 fills, telemetry |
| 4 | 8–10 | Not started | Queue model L3, latency model L4 |
| 5 | 11–12 | Not started | Live path, ZeroMQ, dashboard |
| 6 | 13–14 | Not started | Validation: predicted vs actual fills |
| 7 | 15+ | Not started | ML signal or equities adapter |

**Current test count: 63, all passing, all under ASan/UBSan.**

### What exists and works

| Component | File | State |
|---|---|---|
| `Result<T, E>` | `core/result.hpp` | Done, 2 tests |
| `Price` / `Qty` / `OrderId` / `Side` | `core/types.hpp` | Done, 10 tests |
| `InstrumentSpec` | `core/instrument.hpp` | Done, 2 tests |
| `parseDecimal` / `parseInteger` | `core/text_to_int.{hpp,cpp}` | Done, 25 tests |
| `Clock` | `core/time.{hpp,cpp}` | Done, 4 tests |
| `OrderEvent` / `EventKind` | `feed/events.hpp` | Done, 1 test (thin — see gap below) |
| `decodeBitstampEvent` | `feed/bitstamp_decoder.{hpp,cpp}` | Done, 1 test |
| `writeU8/64` / `readU8/64` | `util/byte_buffer.{hpp,cpp}` | Done, 9 tests |
| `Record` / `buildRecord` | `telemetry/record.{hpp,cpp}` | Done, 1 test |
| `Sink` | `telemetry/sink.{hpp,cpp}` | Done, 8 tests |

### What remains in Slice 1

1. **The capture loop** — `apps/recorder_main.cpp` is still a stub. Nothing wires decoder → record → sink.
2. **A real golden test** — the round-trip test covers a handful of records; the plan's "1000 recorded
   JSON lines, byte identical" test does not exist yet.
3. **Gap detection** — ADR 0006 decided the *algorithm* (event-id chain), nothing implements it.
4. **The live socket** — no WebSocket client. Capture is currently done by a Python script.
5. **Unattended operation** — no reconnect loop, no rotation, no VPS run.

---

## 2. Corrections to the original plan

These are not preferences. Each was forced by evidence and is recorded in an ADR.

| Original plan said | Reality | Why | ADR |
|---|---|---|---|
| Coinbase `full` channel | **Bitstamp `live_orders_btcusd`** | Coinbase L3 now requires Exchange-tier auth, unavailable on a retail account. A 30-second probe caught it. Bitstamp serves per-order L3 publicly, unauthenticated. | 0010 |
| Order IDs are UUID strings; hash or intern them, handle collisions | **Plain `uint64`, parsed from `id_str`** | Bitstamp issues 16-digit numeric ids. The hard problem the plan anticipated does not exist on this venue. Read the *string* form, never the JSON number: generic JSON pipelines coerce to double and lose exactness above 2^53. | 0005 |
| Integer sequence numbers; detect gaps arithmetically | **`event_id` / `pre_event_id` chain** | Bitstamp does not number messages. Detection is a *link* check (does this event's `pre_event_id` match the last `event_id`), not `expected == actual + 1`. A broken chain tells you *that* you lost data, never *how much* — so any policy depending on gap size is unavailable. | 0006 |
| Three event structs: `AddOrder`, `CancelOrder`, `MatchEvent` | **One `OrderEvent` + `EventKind{add, modify, remove}`** | The three venue messages carry identical fields. One trivially-copyable struct keeps the queue and record layouts uniform, and keeps venue vocabulary at the decoder boundary. | — |
| `RecordHeader`, `#pragma pack(push,1)`, 12 bytes | **`Record`, natural alignment, 56 bytes, `static_assert`ed** | Packing buys 7 bytes and costs misaligned access on every field. The size assertion is what actually protects the format; packing was solving a problem this project does not have. | — |
| `class Clock` with `virtual Nanos now() const = 0` | **`struct Clock` holding two `std::function<Nanos()>`** | There is no virtual dispatch anywhere else in this codebase. A struct of callables gives the same test seam with no vtable and matches the value-type style of `Result`, `Price`, `Qty`. | — |
| Tick size "hardcoded, or per instrument config?" | **`InstrumentSpec`, supplied by the caller** | Scale is a property of the venue/instrument pair, not of `Price`. It is never read from a message — venues do not transmit their own scales. | 0004 |
| Error type: "optional, Result, or codes?" | **All three, by case** | `bool` + out-param for a single binary outcome; `optional` when the reason does not matter; `Result<T,E>` when several failures are distinguishable and the caller needs the reason. | 0003 |

### Components the original plan did not anticipate

- **`text_to_int` (`parseDecimal` / `parseInteger`).** The plan treats "int64 ticks not double" as a
  *type* decision. It is also a *parsing* decision: the venue sends `"58356.10"` as text, and the only
  way to reach `5835610` without touching a float is to parse the digits directly. This is ADR 0004's
  guardrail made executable, and it is 25 of the 63 tests — including exact `INT64_MAX`/`UINT64_MAX`
  boundaries and rejection of excess precision.
- **`byte_buffer`.** The plan gestures at "C-style arrays and pointer arithmetic, for the byte buffer".
  Modern equivalent is `std::span<std::byte>` with bounds-checked primitives. Reserved for reading
  *old* record versions once the format changes; the current version uses the memcpy fast path.
- **`Sink`.** The plan has no write abstraction; records just get "appended". Making it a type gets
  RAII file lifetime, `Result`-based open failure, and a place to put the flush-policy decision.

---

## 3. Slice 1 — finishing the recorder

**Ships:** a process that reads a Bitstamp `live_orders_btcusd` stream, decodes each message, and
appends fixed-size binary records, unattended, without leaking or silently losing data.

Order of attack, unchanged and still correct — *never* in parallel:

1. ~~Python script dumps raw JSON lines to a file.~~ **Done** — `scripts/dump_raw_ws_bitstamp.py`,
   real captures in `data/raw/`.
2. ~~C++ decodes those lines into event structs.~~ **Done** — `decodeBitstampEvent`.
3. ~~C++ writes those structs as binary records; verify by reading back.~~ **Done** — `Record` + `Sink`,
   byte-exact round-trip test.
4. **Wire 2 and 3 together into `recorder_main`, reading a file.** ← *you are here*
5. Golden test at scale: 1000 real lines through the whole path.
6. Gap detection per ADR 0006.
7. Only then swap the file reader for a WebSocket client.

### Step 4: the capture loop

This is deliberately small. Every hard part is already built and tested.

```cpp
// apps/recorder_main.cpp -- shape only
// 1. Build the InstrumentSpec for the channel you are capturing.
// 2. Open a Sink for the output path.                     (Result: handle cannot_open)
// 3. For each line of the input file:
//      decodeBitstampEvent(line, spec)                    (Result: 4 DecoderError cases)
//      buildRecord(event, clock)
//      sink.write(record)                                 (bool)
// 4. Flush. Report counts: decoded, skipped, failed, written.
```

Design questions to answer yourself:

- **`not_order_event` is not a failure.** Bitstamp's `bts:subscription_succeeded` is a normal message
  that is not an order. It must increment a *skipped* counter, not an *error* counter. Conflating
  the two makes a healthy capture look broken. What are the other three `DecoderError` cases, and
  should any of them stop the run rather than be counted?
- **Flush cadence.** `Sink` deliberately has no policy. Every record is safest and slowest; every N
  records or every T seconds is the real-world choice. Pick one, write down the data-loss window it
  implies. Note that flush is *not* durability — a power loss can still lose flushed bytes.
- **Counters are the observability surface.** A capture that silently drops 40% of messages looks
  identical to a healthy one unless you count. What is the minimum set of counters that would let
  you notice?
- **Reading lines.** The whole-file-into-one-buffer approach and the line-at-a-time approach have
  different lifetime rules for the `string_view` you hand the decoder. Which did you pick and why?

Traps:

- Treating `not_order_event` as an error, or worse, aborting on it.
- Holding a `string_view` past the buffer that owns it. This is the single most likely UB in this
  step.
- Building a JSON DOM per message. The On Demand API exists to avoid this; the codebase already
  uses it correctly.
- Writing the capture into the repo. Captures are data, not source.

**Test to write:** the golden test the original plan specified. Take 1000 real captured lines, run
the full path, assert the record count matches the count of order-lifecycle lines (not total lines —
the subscription message is legitimately skipped), and assert the file is exactly
`sizeof(Record) * count` bytes. Then read every record back and assert byte-identity against a
re-decode of the same input.

### Step 6: gap detection

ADR 0006 decided the algorithm and named the three complications. None are implemented:

- Hold the previous message's `event_id`; check the next message's `pre_event_id` matches.
- A broken chain gives no magnitude — you know *that* you lost data, not *how much*.
- `bts:request_reconnect` is a *planned* discontinuity and must not be recorded as a data-quality gap.
- Snapshot seeding creates a chain break at the start of every capture that is not a fault.

Design question: does a gap belong *in* the record stream (a distinguished record type) or in a
sidecar log? The first makes replay gap-aware for free and forces `Record` to grow a type field;
the second keeps `Record` at 56 bytes but lets a replay silently ignore the gap. Both are
defensible; ADR 0006's own reasoning leans toward the capture being self-describing.

**learncpp:** 4.6 (fixed-width integers), 4.8 and 6.7 (why you are avoiding floating point), 13.6
(scoped enums), 13.8 (aggregate initialisation), 16.10 (vector capacity), 28.6 and 28.7 (binary
file I/O), O.2 (bitwise operators).

**External:** Bitstamp WebSocket v2 docs, `live_orders` channel — read the `order_created`,
`order_changed`, `order_deleted` semantics until you can draw the lifecycle from memory. simdjson
On Demand docs. Boost.Beast async TLS websocket example (budget three days; least rewarding part of
the project).

---

## 4. Slice 2 — the book builder (weeks 3–5)

**Ships:** `OrderBook` reconstructed from your capture, top-of-book independently verified.

```cpp
class OrderBook {
public:
    void apply(const OrderEvent&);          // one entry point; EventKind selects the path
    Price best_bid() const;
    Price best_ask() const;
    Qty   qty_at(Side, Price) const;
    // invariant: best_bid() < best_ask() at all times outside apply()
};
```

Note the shape change from the original plan: one `apply` taking one `OrderEvent`, not three
overloads, because the decoder already normalised the three venue messages into one type.

**ADR 0007 (book level storage) is still `TODO`.** Decide it in this slice, not before:
flat array indexed by tick offset, hash map, or sorted vector. The array is fastest and is the
canonical design, but decide *and write down* what happens when the market moves outside the
preallocated band.

Design questions that remain exactly as the original plan stated them:

- How do you find an order to cancel in O(1), and who owns the memory it points at?
- FIFO within a price level: which end do new orders join, which end do matches consume? Get this
  wrong and the entire queue model in slice 4 is meaningless.
- Write the invariant list as a `validate()` compiled into debug builds and called after every
  `apply`. This one habit will save a week.
- `order_changed` is not a cancel plus an add. Read the venue docs on whether a size *decrease*
  loses queue priority — this directly feeds slice 4.

**Verification is harder on Bitstamp than the original plan assumed.** The plan says "assert your
top five levels match the `level2` channel". Bitstamp's equivalent is its `order_book`/`diff_order_book`
channel plus the `group=2` REST snapshot. Confirm what independent depth source you actually have
*before* week 3, because a book builder with no independent check is not verifiable.

**learncpp:** 12.7–12.12 (pointers), 15.4 (destructors), 17.9 (pointer arithmetic), 19.1–19.5
(new/delete, dynamic arrays), 20.2 (stack and heap), 21.2/21.3/21.7 (operator overloading),
21.12/21.13 (assignment, shallow vs deep copy), 22.2–22.5 (rvalue refs, move, `unique_ptr`),
23.2/23.4 (composition vs association — this decides who owns what).

**External:** WK Selph, "How to build a fast limit order book". Harris, *Trading and Exchanges*.
Cartea/Jaimungal/Penalva ch. 1–3. Bouchaud/Bonart/Donier/Gould, *Trades, Quotes and Prices* — start
this here, it is the best single source for slice 4. Chandler Carruth, CppCon 2016 on data-oriented
design.

---

## 5. Slice 3 — replay engine v1 (weeks 6–7)

**Ships:** `replay_main` reads a capture, drives a strategy, produces PnL and telemetry.

`Clock` is **already built and does not match the original plan's sketch** — it is a struct of
callables, not an abstract base class. The plan's CI trap still applies and is worth implementing
literally:

> Grep for `std::chrono::system_clock::now()` outside `time.cpp` in CI and fail the build if it
> appears. Five lines, and it enforces the entire design.

The remaining interfaces are unbuilt and **ADR 0009 (dispatch mechanism) is still `TODO`**:

```cpp
class Feed     { public: virtual bool next(OrderEvent& out) = 0; };   // false at end of stream
class Strategy { public: virtual void onBookUpdate(const BookView&) {} /* ... */ };
class ExecutionVenue { public: virtual OrderId submit(const NewOrder&) = 0; /* ... */ };
```

Decide ADR 0009 here: virtual dispatch costs one indirect call per event. For this project virtual
is almost certainly correct, but **you need the argument, not the conclusion** — note CRTP as the
alternative and say why you rejected it.

The SPSC queue belongs to this slice, and its memory ordering is the single most likely deep
technical question from Optiver or Akuna on this whole project:

```cpp
template <typename T, std::size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");
    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
public:
    bool tryPush(const T&) noexcept;
    bool tryPop(T&) noexcept;
};
```

- Which memory order on each load and store, and why is `relaxed` insufficient? Whiteboard-ready.
- Why `alignas(64)` on both indices? **Measure the difference, do not assume it.**
- `BookView` handed to the strategy: make holding it past the callback impossible *by design*, not
  by comment.

Traps: look-ahead bias (applying an event then letting the strategy act on that same timestamp with
information it could not have had); reporting PnL that excludes fees you never modelled — model fees
in this slice even at L1, because fees flip most short-horizon strategies negative.

**learncpp:** 20.1 (function pointers), 24.1–24.7 (inheritance), 25.1–25.9 (virtual functions,
vtable, slicing), 26.1/26.2 (template classes, non-type template params — that is `N`), 18.4
(chrono), 12.15 (`std::optional`).

**External:** Anthony Williams, *C++ Concurrency in Action* 2e, ch. 5 then ch. 7 — non-negotiable,
read before writing the queue. Rigtorp's SPSC write-up (read *after* yours, then diff the reasoning).
Fedor Pikus, CppCon 2017 on atomics. Klaus Iglberger, *C++ Software Design*.

**Test to write:** `NoopStrategy` — full capture through, assert PnL exactly zero, position exactly
zero, events in == events out. Then `BuyOnceStrategy` where you can compute the fill by hand.

---

## 6. Slice 4 — queue position and latency (weeks 8–10)

Where the project stops being plumbing and becomes research. Unchanged from the original plan, and
still the intellectual core.

**ADR 0008 (cancel ambiguity) is still `TODO` and explicitly deferred** until order/trade joining
demonstrates real Bitstamp cancel semantics. That deferral is correct — do not guess it early.

The one hard problem: when you see a cancel at your price level, you cannot observe whether it was
ahead of you or behind you. The standard options:

1. All cancels behind you — optimistic, overstates fill rate.
2. All cancels ahead of you — pessimistic.
3. Uniform across the queue — a cancel of size `q` at a level with total `Q` reduces queue-ahead by
   `q * (ahead / Q)`.
4. Weighted toward the back, since older front-of-queue orders are more likely resting passively.

**Implement at least two, report the spread between them.** That spread is a headline result,
because it quantifies the uncertainty in your own simulator. Most people who claim a backtest never
name this assumption — naming it is a differentiator in interviews.

Latency is not one number: order-out, ack-in, market-data-in, and your own processing time. Which do
you model separately? What distribution — log-normal is the usual start; fit it, plot a QQ plot, be
honest about the tail. Is latency autocorrelated? Real latency is bursty; what does assuming
independence cost you?

**External:** Moallemi and Yuan on queue position valuation. Bouchaud et al. on queue dynamics.
Blitzstein Stat110 on Poisson processes. Little's law and M/M/1. Hawkes processes (Bacry et al.) —
optional, but knowing *why* Poisson is wrong is a good interview answer. Avellaneda–Stoikov if you
pick market making. Almgren–Chriss for impact.

---

## 7. Slice 5 — live path (weeks 11–12)

**Ships:** `live_main`, the same strategy binary, against a sandbox, with ZeroMQ telemetry and a
dashboard.

**Venue correction:** the original plan says Coinbase REST auth. Per ADR 0010 the primary venue is
Bitstamp, so this is Bitstamp's authenticated REST API — and note that ADR 0010 treats external
execution as *optional*. A local paper-execution venue satisfies the deliverable in
`project-plan-v2.md`; live order placement is a stretch goal, not a requirement.

Order state machine: pending-new, acked, partially filled, filled, cancel-pending, cancelled,
rejected. Draw it before coding it. What happens to a cancel arriving after a fill?

Idempotency: if you resend on timeout, how do you avoid double submitting? Client order IDs.

The risk layer sits between strategy and venue and **must run on the replay path too**, or you are
testing different code.

**The strategy binary is identical between replay and live. Prove it.** Same binary, different
config, or you have not achieved the thing this whole project is about.

Trap: do not let the ZeroMQ publish block the strategy thread. Publish from a separate thread
reading a second SPSC queue.

---

## 8. Slice 6 — validation (weeks 13–14)

**This is the deliverable.** Everything else is scaffolding for this number. See
`docs/project-plan-v2.md` §1 for the exact claim being made and the censoring policy for
gap-containing sessions.

```python
# python/research/validate_fills.py
def calibration(predicted_prob, actual_filled): ...   # Brier score + reliability curve
def price_error_bps(predicted_px, actual_px): ...     # distribution, not a mean
def bootstrap_ci(stat_fn, data, n=10_000): ...        # CI on every headline number
```

- A single mean error is not a result. What is the full *distribution*, and is it biased in one
  direction? A systematic bias is more interesting than large variance — it points at a fixable
  modelling error.
- Fill probability is a probabilistic forecast, so accuracy is the wrong metric. **Calibration** is
  right: a model that says 30% and is right 30% of the time is well calibrated.
- How many orders before the number means anything? Compute it, do not guess.
- Does error depend on regime? Split by spread, volatility, time of day. "The simulator is accurate
  except when the spread is wide" is a far better README line than a single average.

**External:** Brier score and reliability diagrams (`sklearn.calibration` practically,
Gneiting–Raftery for proper scoring rule theory). Efron–Tibshirani on the bootstrap. Lopez de Prado,
*Advances in Financial Machine Learning* ch. 11–16 on backtest overfitting. Bailey–Lopez de Prado,
*The Deflated Sharpe Ratio* — report the standard error of your Sharpe and you are ahead of most
candidates. Lehalle–Laruelle, *Market Microstructure in Practice*.

---

## 9. Slice 7 — ML signal or equities adapter (week 15+)

Only after slice 6 is committed and written up.

**If ML:** the signal is a `Strategy` implementation, nothing more. The engine does not change.
Inference through ONNX Runtime's C++ API or libtorch, loaded once at startup, no allocation in the
callback. The hard parts are not the model — they are labelling (triple barrier), purged and
embargoed cross-validation, and not leaking future information through features. Lopez de Prado
ch. 3, 4 and 7.

**If equities adapter:** implement `Feed` and `ExecutionVenue` for Alpaca or IBKR. If your
interfaces were right in slice 3 this is a weekend. If it is not a weekend, that is useful
information about your abstractions.

---

## 10. Why this project reads well for quant roles

The components already built map onto named, standard patterns. Being able to name them is worth as
much as having built them.

| What you built | What it is called in industry | Where else it appears |
|---|---|---|
| `Record` + `Sink`, append-only, versioned | **Write-ahead log / event log** | PostgreSQL, MySQL, SQLite WAL; Kafka segment files |
| Capture then replay to reconstruct state | **Event sourcing** | Standard architecture pattern; also how exchange simulators work |
| Fixed-size records, seek by arithmetic | **Tick capture / market data recording** | Every trading firm; commercial products like Tardis, Databento |
| `parseDecimal` avoiding float on the price path | **Fixed-point / decimal arithmetic** | Every exchange gateway; FIX `PriceType`, DECIMAL columns |
| Version field read before interpreting bytes | **Schema evolution** | Protobuf field numbering, Avro schemas, Kafka format versions |
| `InstrumentSpec` supplied, not inferred | **Reference data / instrument master** | A whole team's job at any real firm |
| `Result<T,E>`, no exceptions on hot path | **Error-as-value** | Rust's `Result`, `std::expected` (C++23), Google's `absl::StatusOr` |

**Interview talking points this repo already earns:**

- *"Why not a double for price?"* — you can answer with a measured example, not a slogan, and you can
  point at the exact commit where the parser rejects excess precision rather than rounding it.
- *"How do you know your capture is correct?"* — byte-exact round-trip test, file size assertion,
  static-asserted record size.
- *"What happens when the format changes?"* — version field, and the explicit fast-path/legacy-path
  split with `byte_buffer` reserved for the legacy decoder.
- *"How did you pick your venue?"* — ADR 0010, including that you probed it in 30 seconds and found
  the documented plan was wrong. Firms care much more about this than about the code.
- *"What is your simulator's biggest weakness?"* — the queue-position cancel ambiguity, quantified as
  a spread between two assumptions rather than hidden.

**The honest gaps to be ready for:** no live socket yet; no book; the validation number that is the
actual deliverable does not exist yet. Say so plainly — a candidate who accurately describes what is
unfinished is more credible than one who oversells.

---

## 11. Maths track running underneath

Unchanged. Thirty to forty minutes a day, in parallel with the code.

| Weeks | Topic | Source |
|---|---|---|
| 1–4 | Probability foundations, distributions, expectation, conditioning | Blitzstein Stat110, lectures 1–12 |
| 5–8 | Poisson processes, exponential inter-arrival, Markov chains | Stat110 Poisson and Markov lectures |
| 8–10 | Queueing intuition, Little's law, heavy tails, distribution fitting | Any intro queueing chapter + `scipy.stats` on your own latency data |
| 11–14 | Estimation, confidence intervals, bootstrap, proper scoring rules | Bootstrap chapter + calibration reading |
| Throughout | Market microstructure | Harris, then Bouchaud et al., then Cartea et al. |

Use your own captured data as the exercise set wherever possible. Fitting a distribution to your
real latency numbers teaches more than fitting one to a textbook dataset, and it feeds directly into
slice 4.

---

## 12. Weekly cadence

- **Mon–Thu:** one slice task per session. Write the test first where the rule is statable in a sentence.
- **Fri:** full suite under sanitisers, tag a commit, write one `docs/decisions/` entry for whatever
  you decided that week.
- **Sat:** maths block, uninterrupted.
- **Sun:** one chapter of the current finance or C++ book; write four sentences on what it changes
  about your design.

At the end of every slice, write the README section for that slice **before** starting the next one.

**Three ADRs are currently `TODO` and each is due in a specific slice:** 0007 (book level storage) in
slice 2, 0009 (dispatch mechanism) in slice 3, 0008 (cancel ambiguity) in slice 4. Do not let them
stay empty past their slice.

---

## 13. What to ask AI for, precisely

Still the right list:

- "Here is my `SpscQueue`. I used `memory_order_release` on the write index. Critique the ordering."
- "I chose flat array price levels with a 5000 tick band. What breaks when BTC moves 10% in a minute?"
- "Explain what this ThreadSanitizer report is telling me."
- "What is the standard argument against my queue cancel assumption?"
- "Quiz me on the vtable, no answers until I have tried."

Two additions that have proved their worth on this project:

- **"Run this and show me what it actually does."** Several bugs here compiled cleanly and read
  correctly. Only execution caught them.
- **"Is this the standard name for what I built?"** Knowing your append-only record file is a
  write-ahead log, and that your capture-replay design is event sourcing, converts work you already
  did into vocabulary an interviewer recognises.

Not: "write the book builder", "implement queue position", "give me the CMakeLists". You already
know why.
