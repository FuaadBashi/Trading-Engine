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

**Reconciled 2026-08-16:** ADR 0011 selects the portable format required by `project-plan-v2.md`
§1E. The shipped 56-byte host-layout writer and in-stream `RecordKind::gap` remain readable legacy
v2 data. Durable v3 uses explicit little-endian headers/records plus one manifest-owned segment
boundary model, as clarified by ADR 0006 Amendment 3. The v3 encoder/decoder is decided but not yet
implemented.

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
| 1 | 1–2 | **Capture path done; socket deferred** | Recorder: venue JSON to binary capture on disk, gap-aware |
| 2 | 3–5 | Not started | Book builder, verified against an independent depth source |
| 3 | 6–7 | Not started | Replay engine v1, L1 fills, telemetry |
| 4 | 8–10 | Not started | Queue model L3, latency model L4 |
| 5 | 11–12 | Not started | Live path, ZeroMQ, dashboard |
| 6 | 13–14 | Not started | Validation: predicted vs actual fills |
| 7 | 15+ | Not started | ML signal or equities adapter |

**Current test count: 80, all passing, all under ASan/UBSan, green under both GCC and Clang in CI.**

### What exists and works

| Component | File | State |
|---|---|---|
| `Result<T, E>` | `core/result.hpp` | Done, 2 tests |
| `Price` / `Qty` / `OrderId` / `Side` | `core/types.hpp` | Done, 10 tests |
| `InstrumentSpec` | `core/instrument.hpp` | Done, 2 tests |
| `parseDecimal` / `parseInteger` | `core/text_to_int.{hpp,cpp}` | Done, 25 tests |
| `Clock` | `core/time.{hpp,cpp}` | Done, 4 tests |
| `OrderEvent` / `EventKind` | `feed/events.hpp` | Done, 6 tests |
| `decodeBitstampEvent` / `decodeBitstampChain` | `feed/bitstamp_decoder.{hpp,cpp}` | Done, 1 direct test + exercised via the recorder tests below |
| `writeU8/64` / `readU8/64` | `util/byte_buffer.{hpp,cpp}` | Done, 9 tests; host-endian today, explicit little-endian v3 work remains (ADR 0011) |
| Legacy `Record` / `RecordKind` / `buildRecord` / `buildGapRecord` (v2) | `telemetry/record.{hpp,cpp}` | Done and tested; portable v3 segment/snapshot encoding remains (ADR 0011) |
| `Sink` | `telemetry/sink.{hpp,cpp}` | Done, 3 tests |
| `runRecorder` (the capture loop) | `telemetry/recorder.{hpp,cpp}` | Done, 12 tests, incl. the golden round-trip test |

### What remains in Slice 1

1. **A C++ consumer for the REST snapshot.** `scripts/dump_raw_ws_bitstamp.py` already fetches
   Bitstamp's `group=2` order-book snapshot at the start of every segment and writes it to
   `segment-NNNN.snapshot`, exactly the "snapshot + diff" pattern real venue integrations need
   (this already implements ADR 0006's resync story — it was missed in an earlier pass of this
   document, corrected in §4). What's still missing is entirely on the C++ side: nothing parses a
   `.snapshot` file or seeds an `OrderBook` from it before applying `OrderEvent`s. This is now
   Slice 2 work, not a Slice 1 gap.
2. **The live socket** — no WebSocket client. Capture is currently done by the Python script above.
3. **Unattended operation** — no reconnect loop, no rotation, no VPS run. Depends on the live socket.

Everything else originally listed here — the capture loop, the golden byte-exact test, and gap
detection — shipped this slice; see the component table above.

---

## 2. Corrections to the original plan

These are not preferences. Each was forced by evidence and is recorded in an ADR.

| Original plan said | Reality | Why | ADR |
|---|---|---|---|
| Coinbase `full` channel | **Bitstamp `live_orders_btcusd`** | Coinbase L3 now requires Exchange-tier auth, unavailable on a retail account. A 30-second probe caught it. Bitstamp serves per-order L3 publicly, unauthenticated. | 0010 |
| Order IDs are UUID strings; hash or intern them, handle collisions | **Plain `uint64`, parsed from `id_str`** | Bitstamp issues 16-digit numeric ids. The hard problem the plan anticipated does not exist on this venue. Read the *string* form, never the JSON number: generic JSON pipelines coerce to double and lose exactness above 2^53. | 0005 |
| Integer sequence numbers; detect gaps arithmetically | **`event_id` / `pre_event_id` chain** | Bitstamp does not number messages. Detection is a *link* check (does this event's `pre_event_id` match the last `event_id`), not `expected == actual + 1`. A broken chain tells you *that* you lost data; magnitude is only recoverable modulo 4 (ADR 0006 Amendment 1), not exactly — so the policy still treats any break as fully invalidating. | 0006 |
| Three event structs: `AddOrder`, `CancelOrder`, `MatchEvent` | **One `OrderEvent` + `EventKind{add, modify, remove}`** | The three venue messages carry identical fields. One trivially-copyable struct keeps the queue and record layouts uniform, and keeps venue vocabulary at the decoder boundary. | — |
| `RecordHeader`, `#pragma pack(push,1)`, 12 bytes | **Legacy `Record` is 56 host-layout bytes; durable v3 is explicit little-endian** | Packing and `static_assert` protect neither cross-ABI layout nor byte order. ADR 0011 keeps v2 readable but separates the in-memory object from the permanent 64-byte record schema. | 0011 |
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

### Step 6: gap detection — legacy implementation done; durable segmentation decided

**Shipped in v2:** the link check (`chain.pre_event_id == previous.event_id`), an in-stream
`RecordKind::gap`, and the three false-gap guards (first event, protocol messages, unreadable
chain). **Durable v3 decision:** ADR 0006 Amendment 3 and ADR 0011 choose the manifest segment as
the single continuity boundary. A gap closes the segment; the revealing event stays only in raw
evidence and is not applied or written into the valid binary prefix. Existing v2 gap records remain
readable as legacy data.

**One overstated claim, corrected:** "a broken chain gives no magnitude" is ADR 0006's stated
*policy* (any gap fully invalidates the book regardless of size), but not quite the full *fact*.
Amendment 1 to ADR 0006, from an investigation on an hour-long capture, found the `event_id`'s
final UUID segment is a dense four-state counter, so the number of lost events is recoverable
**modulo 4** — not exact, and not enough to distinguish a 1-event drop from a 4n+1-event burst, so
the policy is correctly unchanged. But "no magnitude at all" is stronger than what was actually
found; corrected here and everywhere else this document said it flatly.

**One follow-up from ADR 0006/0011 is not yet built:** build the ADR 0011 v3 explicit little-endian
segment/snapshot encoder, decoder and golden bytes. The manifest owns snapshot/reconnect/gap
boundaries; v3 does not add competing in-stream boundary records. The raw capture already schedules
the websocket drain before the REST snapshot request and drains concurrently while that request is
in flight, satisfying the startup rule measured in ADR 0006 Amendment 2.

**learncpp:** 4.6 (fixed-width integers), 4.8 and 6.7 (why you are avoiding floating point), 13.6
(scoped enums), 13.8 (aggregate initialisation), 16.10 (vector capacity), 28.6 and 28.7 (binary
file I/O), O.2 (bitwise operators).

**External:** Bitstamp WebSocket v2 docs, `live_orders` channel — read the `order_created`,
`order_changed`, `order_deleted` semantics until you can draw the lifecycle from memory. simdjson
On Demand docs. Boost.Beast async TLS websocket example (budget three days; least rewarding part of
the project).

---

## 4. Slice 2 — the book builder (weeks 3–5)

**Ships:** `OrderBook` reconstructed from your capture, top-of-book independently verified against
a source you didn't build yourself.

### Resolve first: the C++ side has no snapshot consumer yet

**Correction to an earlier version of this section:** it claimed no snapshot mechanism existed
anywhere in this project and that fixing it "likely belongs partly in Slice 1." That was wrong,
found by checking only the `.jsonl` file and never the sibling files sitting next to it. Corrected
here rather than left standing.

`scripts/dump_raw_ws_bitstamp.py`'s own docstring: *"Capture public Bitstamp L3 events as
snapshot-backed segments... each segment has its own `group=2` REST snapshot and payload-only JSONL
stream... This implements ADR 0006."* Every capture directory already has three files, not one:

```text
segment-0000.jsonl       the live_orders diff stream (what earlier analysis only looked at)
segment-0000.snapshot    one REST GET .../order_book/btcusd/?group=2, fetched at segment start
manifest.json            ties the two together: snapshot microtimestamp, chain_valid, event_id range
```

The reference capture's snapshot, read directly rather than assumed: **4,174 bids, 4,563 asks**,
each row `[price_str, amount_str, order_id_str]` — e.g. `["64840.11", "0.22992207",
"2037492791283712"]`. Best bid `64840.11`, best ask `64840.12`: a one-cent top-of-book spread. That
number is easy to conflate with the much wider figure in ADR 0007 below, and they are not the same
thing — the ADR's figure is price *dispersion* across every resting order in the book (and across
observed events over 29 seconds), not the inside market. The snapshot's own extremes matter
directly for that ADR: bids as low as **$0.01**, asks as high as **$483,980,000.00**.

So the "snapshot + diff" pattern real venue integrations need (Bitstamp's `live_orders` channel
itself sends no initial state over the websocket, confirmed earlier against Tardis.dev's own
Bitstamp integration notes) is already built here — one layer down, in Python, not yet consumed by
any C++. What's actually missing for Slice 2:

1. A parser for `.snapshot`'s `bids`/`asks` rows into initial resting orders.
2. A seed step that loads all of them into `OrderBook` before any `OrderEvent` from the paired
   `.jsonl` is applied.
3. Confirmation of the ordering rule: only apply `.jsonl` events at or after the snapshot's own
   `microtimestamp` (`1786269861574036` for the reference capture) — `dump_raw_ws_bitstamp.py`'s own
   comments say diffs at or before that timestamp should be discarded, since the snapshot already
   reflects them.

**One measured startup-boundary effect to preserve before writing `validate()`:** the reference
`.jsonl` contains five deletes for IDs absent from the snapshot, all between lines 2 and 178. The
first is order `2037493297635328`, 373ms after the snapshot microtimestamp. This is the same warm-up
phenomenon ADR 0006 Amendment 2 measured at scale: 10 unknown resting-order deletes through line
5,153 (35.5 seconds after the snapshot) before the later chain break. It is not evidence that the
current capture blocked websocket reads during the REST request. Waiting for
`bts:subscription_succeeded` remains an optional experiment, not a demonstrated fix or a
prerequisite for `validate()`.

**Regardless of exactly how the seed step is wired, `OrderBook` must not silently absorb a
`modify`/`remove` that references an `OrderId` it has never seen.** Count it — the same instinct as
Slice 1's `skipped`/`failed` counters. The Python bootstrap oracle now measures five such deletes in
the short reference segment. It also found two important Bitstamp rules that the C++ book must
honour: a same-ID `order_changed` may move price, and an `order_deleted` may carry a replacement or
execution price rather than its stored resting price. Locate deletion by ID and remove the stored
level/quantity; never subtract the delete payload from the payload's reported price.

### Interface

```cpp
class OrderBook {
public:
    Result<ApplyOutcome, ApplyError> apply(const OrderEvent&);
    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;
    Qty   qtyAt(Side, Price) const;
};
```

ADR 0012 settles the behaviour: one `apply`, because the decoder already normalized Bitstamp's
three message types into one `OrderEvent`; reasoned errors for duplicate, unknown or contradictory
events; optional best prices because a side can be empty; zero from `qtyAt` when no level exists.

Deliberately gap-agnostic. `OrderBook` knows nothing about `RecordKind` or capture files — it
only ever sees `OrderEvent`s, the same as its unit tests will feed it directly. Recognising a
legacy `RecordKind::gap` or a v3 segment boundary and deciding when to discard and reseed the book
is the replay/live driver's job. Keeping that logic out of `OrderBook` keeps book correctness
independent from capture bookkeeping.

### ADR 0007 — price level storage

**Decided.** `std::map<Price, PriceLevel>` per side plus `std::unordered_map<OrderId, OrderLocator>`
for direct lookup, as the correctness reference — no price band, no custom pool or intrusive list
yet. See `docs/decisions/0007-book-level-storage.md` for the full decision and consequences. The
analysis below is kept as the reasoning that led there, not as an open question.

The evidence for it changed over the course of this analysis — an earlier price figure here was
wrong (computed from a handful of events, not the whole file), and a second, independent data point
turned up that mattered more than the one it replaced.

**The corrected number.** Scanned across all 1,433 order events in the reference capture, not a
sample: prices range **$58,356.10 to $66,785.32** — $8,429.22, or 842,922 one-cent ticks, inside
29 seconds. Some of that is genuinely deep resting orders rather than top-of-book movement, and it
is *not* the bid-ask spread — the actual top-of-book spread at the snapshot moment (§ above) is one
cent, `64840.11` / `64840.12`. Conflating "how far apart bid and ask are" with "how wide the prices
in this capture are" would be a real error; worth being precise about the distinction going forward,
even in casual description.

**The number that actually settles the "how big a band" question is the snapshot's own extremes,
not the capture's:** real resting bids down to **$0.01**, real resting asks up to
**$483,980,000.00**. Any flat array sized to cover the *observed trading range* is still an array
that cannot represent orders that are, right now, genuinely resting in the book far from the touch.
Whether that's acceptable depends on what the book promises: if `qtyAt(Side, Price)` is meant to
answer correctly for every resting order, a bounded array is making a silent promise it cannot keep
for a real, present part of the book — not a hypothetical edge case, a measured one.

The options, sharper than the three lines currently in the ADR file:

- **Flat array indexed by tick offset from a base price** — the fastest option, and the canonical
  one: WK Selph's design (your own reading list) is built on it. `index = price.ticks - base.ticks`
  *is* the lookup, and adjacent levels sit in adjacent memory, so "give me the best 5 levels" is a
  sequential scan instead of pointer-chasing. Only realistic if the deep-order problem above is
  either accepted (out-of-band orders are rejected and counted, deliberately not represented) or
  solved with **rebasing** — reallocate centred on the current price when it nears an edge, migrate
  existing levels, swap in. Rebasing does not solve the fundamental range problem on its own, since
  the array still cannot hold a `$0.01` bid and a near-touch level simultaneously at one-cent
  resolution without an enormous allocation.
- **Sparse: hash map for price → level, plus a separately maintained sorted index of occupied
  prices for best-bid/best-ask and top-N.** No range problem at all — a `$0.01` bid and a
  `$64,840.11` bid coexist trivially, because nothing is preallocated between them. The cost is the
  cache-miss-per-level issue named below, and now two structures to keep in sync (the map and the
  sorted index) instead of one.
- **Sorted vector** — same locality as the array for the top of book, no range problem, but a new
  price level costs an O(n) shift, and new levels appear most often during volatile stretches.
- **`std::map` reference implementation (selected)** — O(log n), no range problem and one ordered
  source of truth. Its per-node allocations and pointer chasing are accepted in the reference book
  because simple ownership, stable locators and testability are the immediate requirements. Those
  costs become evidence to measure when an optimized candidate exists.

**Decision consequence.** Reject-and-count on a flat array is unacceptable for the complete
reference book because it would look healthy while omitting real orders. If a later dense window is
measured to help, out-of-window orders go to sparse overflow; the window rebases only when best
prices approach an edge; inability to represent the full state invalidates the book.

### The order-id lookup

Not stated as a type in the original plan, and worth being specific about:
`std::unordered_map<OrderId, OrderNode*>` is the obvious first reach for "cancel an order in O(1),"
and it's fine to start there — get the book correct first. But know why a real system usually
replaces it: `unordered_map`'s buckets are separately heap-allocated nodes, which reintroduces
exactly the cache-miss problem the array was chosen to avoid for price levels. The industry answer
is an **open-addressing (flat) hash map** — no per-bucket allocation, the whole table lives in one
contiguous block. Building one yourself is a legitimate, well-scoped exercise once the book itself
works; it is not something to block Slice 2 on before there is a book to profile in the first place.

### Sizing `Pool<T>`

The plan's skeleton returns `nullptr` on exhaustion rather than growing — right, and consistent with
this project's no-exceptions convention. What it doesn't say is how you pick `N`. Don't guess a
round number: replay your own capture, track the maximum count of simultaneously-resting orders at
any point, and size the pool to that with real headroom. Write the measured number and the headroom
you chose into the same commit that sets `N` — "derived from measurement" is a materially stronger
interview answer than "seemed like enough," and it's the same "use your own captured data" habit
already in §11's maths track.

### FIFO and queue priority

Still a design question to confirm against Bitstamp's own docs specifically, but here is the exact
mechanism to check for, not just "read the docs": on most price-time-priority venues, a size
**decrease** on a modify preserves the order's place in the queue, while a size **increase** is
treated as newly-arrived quantity and goes to the back — otherwise a trader could jump the queue by
disguising a fresh order as a "modify" of an old one. Confirm whether Bitstamp's `order_changed`
draws this same distinction before deciding how `OrderBook::apply` handles it; this is not a detail,
it is the entire input Slice 4's queue-position model will be built on, and getting it wrong there is
invisible until the numbers it produces are wrong.

### `validate()`

The plan says to write one; here is a starting invariant list so "the book's invariants" isn't left
abstract:

- A stable resting-book checkpoint has `bestBid() < bestAsk()` whenever both sides are non-empty.
  Do not assert this blindly after every raw `live_orders` event: the measured stream exposes a
  marketable order's create before its matching change/delete, so an intermediate raw lifecycle
  can cross the displayed book. The decoder/controller must distinguish those transient events
  before the strategy receives a decision-ready `BookView`.
- Every price level's cached total quantity equals the sum of its resting orders' quantities —
  catches the class of bug where a cached aggregate drifts from what it's supposed to summarise.
- Every order reachable from the id-lookup table is also reachable by walking its price level's
  list, and vice versa — no orphans in either direction.
- No `OrderId` is live in the lookup table more than once at a time.

Compiled in debug builds only, called after every `apply`, exactly as the plan says.

### Verification against ground truth

The real industry term for what your golden test does — reconstruct independently, then
continuously diff against a trusted source before believing your own output — is **shadow testing**
(sometimes "parallel run"), standard practice anywhere a new system replaces or shadows one already
trusted. Bitstamp's `order_book`/`diff_order_book` channel plus the `group=2` REST snapshot is your
trusted side. Confirm you can actually pull one of these *before* week 3 — a book builder with no
independent check is not verifiable, only plausible.

### Traps

- Heap-allocating a node per event — the thing `Pool<T>` exists to prevent.
- Treating `order_changed` as a new order with no identity — the same ID can change quantity and,
  as measured in the hour corpus, price. Moving levels is required for L2 correctness; its FIFO
  priority consequence remains explicitly unknown until ADR 0008 is resolved.
- Silently absorbing a `modify`/`remove` on an `OrderId` you've never seen — count it. It's the
  visible symptom of the bootstrapping problem above, not a case to swallow.
- Not handling crossed or out-of-order messages — `validate()`'s first invariant exists specifically
  to catch this rather than let it pass silently.
- Forgetting that a legacy `RecordKind::gap` or a v3 manifest segment boundary means the old book
  cannot be trusted until it is reseeded — the driver's responsibility, not `OrderBook`'s.

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
- *"How do you know your capture is correct?"* — raw chain validation and snapshot-backed segments
  today; durable v3 additionally requires declared sizes, counts and SHA-256 manifest bindings.
- *"What happens when the format changes?"* — ADR 0011 keeps the host-layout v2 reader as legacy and
  gives durable v3 explicit little-endian headers/records with separate versioned decoders.
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
