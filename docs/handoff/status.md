# Status handoff

Written for whichever Claude session picks this project up next. Built by reading the actual repo
state at `HEAD` (`bdbb539`, 2026-08-22) plus the design conversation that led to it — not from memory
of the chat alone. Treat `docs/project-plan-v4.md` as the authoritative long-range plan; this file is
the shorter "what's actually true right now" companion to it, since v4's own baseline table
(section 2) already predates the work described below and hasn't been refreshed.

Verify before trusting: re-run the build/test commands at the bottom before relying on anything here
as still current.

## Build state right now

Clean build, 166/166 CTest cases pass locally (GCC via the default toolchain; CI's dual GCC/Clang
+ ASan/UBSan matrix not re-verified in this pass — see `Runtime checking` in v4 §2).

## What's actually done

### The Bitstamp adapter is namespaced and folder-scoped
`include/te/feed/bitstamp/` and `src/feed/bitstamp/` hold everything venue-specific, all under
`namespace te::bitstamp`, with the `Bitstamp` prefix dropped from identifiers since the namespace now
carries that context: `EventClassifier`, `decodeEvent`, `decodeChain`, `decodeTrade`, `parseSnapshot`,
`bootstrap`. Venue-neutral core (`OrderBook`, `OrderEvent`, `TradeEvent`, `TradeReconciler`, `Result`)
stays untouched by this and lives directly under `te`. `VenueId::bitstamp` and prose references to the
real venue in comments/ADRs were deliberately left alone — only identifier/file naming changed.

### `bitstamp::bootstrap()` — one-time cold start, done
`include/te/feed/bitstamp/bootstrap.hpp` / `src/feed/bitstamp/bootstrap.cpp`. Takes a `BookSnapshot`
and an optional `TradeReconciler*` (default `nullptr`). Seeds a fresh `OrderBook` from the snapshot's
flat order list, and — this was the fix made this session — if given a reconciler, calls
`observe()` on each seeded order too, so a pre-existing resting order the live stream never announces
is still known to the reconciler. `apply()` runs and must succeed before `observe()` is called on the
same event, not the other order — this was a deliberate correction to the original design sketch, and
it's the same ordering principle the replay controller below also depends on.

### `TradeReconciler` — the fill-correction primitive, done
`include/te/feed/trade_reconciler.hpp` / `src/feed/trade_reconciler.cpp`. Keeps its own record of which
orders it believes are currently resting (side/price/quantity, by id), built from the same `OrderEvent`s
`OrderBook` sees via `observe()`. `reconcile(TradeEvent)` checks both the trade's buy-side and sell-side
order id against that record and returns zero, one, or two corrective `OrderEvent`s (`remove` on full
consumption, `modify` with the remainder on partial). Never manufactures an `add` — only ever corrects
an order it already knew about.

### `bitstamp::Replay` — the merge/replay controller, first version done
`include/te/feed/bitstamp/replay.hpp` / `src/feed/bitstamp/replay.cpp`, `tests/unit/test_bitstamp_replay.cpp`
(8 tests, all passing). This is what was being designed by hand across most of this conversation, later
written independently. Signature:

```cpp
Result<ReplayResult, ReplayError> Replay::replay(
    BookSnapshot seed,
    const std::vector<OrderEvent>& orderEvents,
    const std::vector<TradeEvent>& tradeEvents,
    std::uint64_t cutoffMicros);
```

What it does: bootstraps internally from `seed`, then walks both already-decoded vectors with two
indices, comparing `venue_timestamp_us` — earlier wins, exact tie goes to the order event, matching
the ordering discipline established in this session (`apply()` before `observe()`, never `observe()` a
correction). Skips anything at or before the seed timestamp; stops at `cutoffMicros`. Tracks which
order ids were removed by a trade correction (`correctionRemovals`) so a later raw `live_orders` delete
for the same id/side is recognized as redundant and counted (`redundantOrderRemovals`) rather than
treated as a real failure — a more careful version of "tolerate the expected no-op" than what was
originally sketched. Returns `ReplayStats` (counts: read/applied per stream, corrections
generated/applied, redundant removals) alongside the book, rather than the bare `void`/no-stats shape
first proposed — closer to `RecorderStats`'s precedent, which is what this session's review was pushing
toward. Rejects out-of-time-order input outright (`ReplayError::order_input_not_time_ordered` /
`trade_input_not_time_ordered`) rather than trying to reorder it.

### Joined-capture Python tooling exists and matches the plan's field contract
`scripts/dump_raw_ws_bitstamp.py` and `scripts/validate_joined_capture.py` (new, 732 lines) implement
the frame contract from `docs/project-plan-v4.md` §6 field-for-field: `captureOrdinal`,
`localWallTimestampNanos`, `localSteadyTimestampNanos`, `streamKind`, `runId`, `segmentId` are all
present and assigned per accepted frame. `validate_joined_capture.py`'s own docstring: it proves a
`segment-NNNN.jsonl` (raw payload) and `segment-NNNN.frames.jsonl` (provenance) pair still agree —
one consecutive ordinal, order-chain checks scoped to the order stream only, hashes/sizes matching the
manifest. Has its own test file, `tests/python/test_validate_joined_capture.py`.

## What's partially done — the real gap to know about

**The C++ side does not yet consume `captureOrdinal` at all.** `OrderEvent` and `TradeEvent`
(`include/te/feed/events.hpp`, `include/te/feed/trade_event.hpp`) have no such field — grep confirms
it. The plan's own declared deterministic replay key is `(venueTimestampMicros, captureOrdinal)`
(v4 §6), but `Replay::replay()` currently only ever compares `venue_timestamp_us`, falling back to a
fixed "order always wins an exact tie" policy instead of consulting a real capture-arrival ordinal.
That policy is a reasonable provisional stand-in — and was arrived at independently in this session's
design discussion before `captureOrdinal` existed anywhere in the plan — but it is not what v4 §6/§7
specifies as the real tie-break. The Python capture layer is already producing the field; nothing on
the C++ decode/replay path reads it yet.

**`Replay` covers most, not all, of v4 §7's contract.** Checked against the "Real order event" /
"Trade event" numbered lists in `docs/project-plan-v4.md` §7:

- Done: select-earlier-of-two, tie→order, classify→apply→observe-on-success, reconcile→apply
  corrections, never `observe()` a correction, count the redundant-removal case by name.
- Not done: a documented ADR for tie-breaking/reorder-window/backward-timestamp policy (§7's "must
  have an ADR" line — no such ADR exists yet); the book-health state machine
  (`unseeded → warming → valid → stale_or_gapped → resyncing → valid`, v4 §7 "Book health") — nothing
  like this exists on `OrderBook` or `Replay` today; gap/reconnect/reseed behavior — explicitly out of
  scope per the design PDF, and still unaddressed in code; repeated-run digest/hash reproducibility —
  the Stage 2 gate in v4 §12 ("ten runs over the same fixture produce identical event and final-book
  hashes") has no corresponding test yet.

**Stale comment left behind.** `src/feed/bitstamp/bootstrap.cpp` has a comment — *"Replay that read
order JSONL sequentially that takes the orderbook and the reconciler here and then use the info if
timestamp is > T"* — describing an earlier version of the design (raw JSONL text streams) that isn't
what `Replay::replay()` actually consumes (it takes already-decoded `std::vector`s instead). Worth
deleting or rewriting so it doesn't mislead the next reader.

## What's not started

Per `docs/project-plan-v4.md`'s own stage roadmap (§9-§12), in stage order:

- **Stage 0** (truth/contract cleanup): receipt-timestamp type/naming fix, `id` vs `id_str` mismatch
  rejection (`src/feed/bitstamp/decoder.cpp` still trusts `id_str` alone — confirmed still true),
  structural-vs-decision-ready validation split, CI guard activation, the mandatory small fixture. Not
  started.
- **Stage 1** (joined capture) is further along than its own plan doc's baseline table suggests — see
  above — but the *validator proving one coherent segment* gate (v4 §11) hasn't been run/confirmed
  end-to-end against a real capture in this pass.
- Everything from **Stage 3 onward** (golden correctness closure, durable v3 corpus, replay/accounting
  engine, queue-position labels, held-out validation, performance lab, live/paper path, dashboard) is
  still ahead, per the plan.

## Suggested next step

Two independent, small threads, either is a reasonable place to pick back up:

1. Decide how `captureOrdinal` reaches the C++ side — probably a new field on `OrderEvent`/`TradeEvent`
   or a wrapping "frame" type — and switch `Replay`'s tie-break from the fixed "order wins" policy to
   the documented `(venueTimestampMicros, captureOrdinal)` key. Write the ADR v4 §7 says the controller
   needs before doing this, since the tie-break policy is exactly the kind of decision an ADR is for.
2. Delete/rewrite the stale comment in `bootstrap.cpp`, and consider whether `Replay`'s current
   fixed-vector inputs (`std::vector<OrderEvent>`, `std::vector<TradeEvent>`) are the final shape, or a
   deliberate stepping stone before wiring in the real `Feed` interface (still an empty stub — Slice 3 /
   Stage 5 territory) or a captureOrdinal-aware frame source.

## Where to read more

- `docs/project-plan-v4.md` — the authoritative plan (stages, gates, contracts). Read §6-§7 and §11-§12
  before touching capture or replay code.
- `docs/pdf/Trading_Engine_Order_Trade_Merge_Deep_Dive.pdf` — the problem/industry-context/plan document
  produced during this session, written before `Replay` existed. Its design (peek/compare/apply-before-
  observe/reconcile-then-apply, never observe a correction) matches what got built almost exactly; its
  build-order and edge-case list are still a reasonable checklist to test `Replay` against.
- `docs/decisions/0010-venue-selection.md` — why Bitstamp specifically, and why the venue-neutral core
  vs. venue-specific-adapter split exists at all.
- `docs/coding-plan-v3.md` — the older Slice-numbered plan; superseded by v4's Stage numbering, but
  still useful for the lower-level "how do I actually implement X" guidance v4 doesn't repeat (e.g. the
  SPSC queue memory-ordering questions under the old Slice 3).

## Commands to reverify this file

```bash
git log --oneline -5
cmake --build build -j
ctest --test-dir build --output-on-failure
```
