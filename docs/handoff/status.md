# Status handoff

Written for whichever Claude session picks this project up next. Built by reading the repo at `HEAD`
(`b635e9c` + working tree, 2026-08-28) and running the tests, not from memory of any conversation.

`docs/project-plan-v4.md` is the authoritative long-range plan. This file is the shorter "what is
actually true right now" companion, because v4's own baseline table (§2) predates most of the work
below.

Verify before trusting: re-run the commands at the bottom.

## Build state

Clean build from scratch, **194/194 CTest cases pass**, plus 8 Python tests (Apple Clang, default toolchain). The CI
dual GCC/Clang + ASan/UBSan matrix has not been re-verified in this pass.

## The most recent piece of work: ADR 0013

Worth reading first, because it changed four components and is the reason the current tests look the
way they do.

**The bug.** Bitstamp announces every fill **twice** — once on `live_orders` (the `amount_traded`
field on an order message, whose `amount` is already post-fill) and once on `live_trades`. The
reconciler treated the trade as fresh news and subtracted the same fill a second time, wrongly
deleting 9 live orders on the reference capture and aborting replay with
`unexpected_order_apply_failure`.

**The fix.** `TradeReconciler` keeps a *fill ledger* of credits keyed by
`(orderId, venueTimestampMicros)`. An order event's `amount_traded` adds a credit; a trade consumes
the credit at its own timestamp and corrects only the **uncovered** part. A trade whose fill the
venue already reported produces no correction.

**Why the timestamp is in the key** — this is the non-obvious part. A single running credit per order
is wrong: a leftover debt from an unmatched fill at T1 contaminates an unrelated fill at T2. The
counter-example is written out in the ADR. Credits at different timestamps must be isolated; within
one timestamp the credit is a fungible pot, which is required because 10 orders in the reference
capture carry more than one fill at the same microsecond.

**The tie-break is no longer load-bearing.** Order events still win an exact timestamp tie (the
reconciler must know about an order before a trade can ask about it), but corrections are now
idempotent, so processing order no longer changes the resulting book.

## What is done and verified

| Area | Where | Evidence |
|---|---|---|
| Venue-namespaced adapter | `include/te/feed/bitstamp/`, `src/feed/bitstamp/`, all in `te::bitstamp` | core (`OrderBook`, `OrderEvent`, `TradeEvent`, `TradeReconciler`, `Result`) has no dependency on it |
| Order/trade/chain/fill decoding | `bitstamp/decoder.cpp`, `trade_decoder.cpp` | `decodeOrder`, `decodeChain`, `decodeTrade`, `decodeFill` |
| Snapshot parsing | `bitstamp/snapshot.cpp` | `parseSnapshot`, synthetic + real snapshot tests |
| Cold start | `bitstamp/bootstrap.cpp` | seeds `OrderBook` and, given a `TradeReconciler*`, seeds its records too |
| Fill correction | `feed/trade_reconciler.cpp` | fill ledger, shortfall corrections, two health counters |
| Merge controller | `bitstamp/replay.cpp` | two-pointer merge by venue time, order-wins-tie, `ReplayStats` |
| Input accounting | `bitstamp/replay.cpp` | `beforeSeed + read + afterCutoff == input size`, asserted on the real capture |
| Determinism fingerprints | `OrderBook::digest()`, `ReplayStats::appliedEventDigest` | FNV-1a over semantic fields; ten-run identical-digest test |
| ID cross-check | `bitstamp/decoder.cpp` | ADR 0005: `id` vs `id_str`, `DecoderError::id_mismatch` |
| Joined capture loading | `bitstamp/joined_capture.cpp` | manifest + payload/frames join, decodes into `CapturedOrderEvent` |
| Joined capture tooling (Python) | `scripts/dump_raw_ws_bitstamp.py`, `scripts/validate_joined_capture.py` | implements the v4 §6 frame contract; own pytest file |

`decodeFill` is a **separate decode** on the same line, following the `decodeChain` precedent, so
`amount_traded` never enters `OrderEvent`. That keeps `OrderEvent` at 40 bytes and
`static_assert(sizeof(Record) == 56)` intact — adding a field would have forced a v3 on-disk format
bump as a side effect of a bug fix.

### Current `Replay` signature

```cpp
Result<ReplayResult, ReplayError> Replay::replay(
    BookSnapshot seed,
    const std::vector<CapturedOrderEvent>& orderEvents,   // event + amountTraded + captureOrdinal
    const std::vector<CapturedTradeEvent>& tradeEvents,   // event + captureOrdinal
    std::uint64_t cutoffMicros);
```

It deliberately does **not** take a `JoinedCapture`. `JoinedCapture` carries a checkpoint, which is
the golden test's *oracle* and something `Replay` never uses; requiring one would force live mode and
unit tests to invent a fake. Callers unpack `JoinedCapture` at the call site.

## The measured result on real data

`BitstampJoinedCapture.RealCaptureReplaysToCheckpointWithNoResiduals`, over
`data/raw/bitstamp-btcusd-20260822T000512Z` (29,404 order events, 84 trades, ~60 seconds):

| | Before ADR 0013 | Now |
|---|---|---|
| replay outcome | `unexpected_order_apply_failure` | completes |
| `unknown_order_id` failures | 9 | **0** |
| checkpoint level mismatches | n/a (aborted) | **0 of 4,533** |
| `redundantOrderRemovals` | — | 0 |
| `ordersRemovedWithUnmatchedFill` | — | 0 |
| `staleFillsDiscarded` | — | 0 |

The replayed book reproduces the independently fetched S1 snapshot exactly, with no hand-listed
exceptions. Capture data is gitignored, so the test skips rather than fails when absent.

## Read this before claiming the reconciler is validated

**`correctionsGenerated` is 0 on that capture.** `live_orders` reports every fill in those 60
seconds, so the credit covers all 82 trades and the reconciler never fires. It is not being
over-suppressed — over-suppression would leave excess quantity resting and show up as mismatches, and
there are none.

But it means the zero-residual result validates the **loader, bootstrap, classifier, merge ordering
and `OrderBook`**. It says nothing about corrections firing when they should. Only the unit tests in
`test_trade_reconciler.cpp` and `test_bitstamp_replay.cpp` cover that path.

**And it may not be reachable on this venue.** Across 1,059 seconds of joined capture and 753 trades,
**637 of 637** trades against a resting order had their fill already reported by `live_orders` via
`amount_traded`. Zero uncovered. A correction can only fire on an uncovered fill, so the path has
never been reached on real Bitstamp data — see ADR 0013, "The correction path has not been reached on
real data", for the per-segment numbers.

Keep the reconciler: it is correct, cheap, unit-tested, and other venues do not all report post-fill
quantities. But treat it as **insurance, not a validated path**, and do not go capturing indefinitely
in the hope of catching one.

That also unsettles the explanation for the three hardcoded adjustments in `test_golden_replay.cpp`.
They were assumed to be orders fully filled with no `order_deleted`; if `live_orders` reports every
fill, that is unlikely. They are now **unexplained**, and cannot be diagnosed from that fixture —
it is order-only and the window is historical. Open candidates: `order_subtype` semantics,
liquidations, a venue-side move between levels.

## Open work

**Highest value first:**

1. **A capture that reaches the correction path.** Still the largest evidence gap — see above.
   Not worth hunting on Bitstamp; a venue that omits fill reporting would exercise it immediately.

2. **Amend plan v4 §6.** It declares the replay key as `(venueTimestampMicros, captureOrdinal)`.
   The ordinal is now decoded and carried, and measurement says the plan is wrong for this venue:
   Bitstamp's trade frame arrives before its matching order frame in **394 of 427** shared
   timestamps, so ordering by ordinal runs `reconcile` before `observe` and produces 41 apply
   failures on the reference capture. The code keeps order-wins-tie deliberately; the plan is what
   needs changing. See ADR 0013, "`captureOrdinal` is carried, but is deliberately NOT the
   tie-break".

3. **Rest of Stage 0 cleanup (v4 §10)** — receipt-timestamp type/naming, structural-vs-decision-ready
   validation split, CI guard activation, the mandatory small fixture. None started.

**Deliberately deferred, documented as such in ADR 0013:** the book health state machine
(`unseeded → warming → valid → stale_or_gapped → resyncing → valid`) and any reorder window. After a
gap a quantity decrease may reflect fills that were never observed, and no policy covers that. It
needs its own decision and its own evidence — do not guess at it.

**Partly addressed since:** the *capture* side of gap/reseed handling now has a rule — a segment's
seed snapshot must not predate its first captured event, enforced by refetch and by ending the
segment as `snapshot_never_overlapped_stream` rather than emitting a holed seed. The *replay* side
(what the engine should do when it meets a gap) is still undecided.

## Where to read more

- `docs/decisions/0013-merge-ordering-and-fill-double-counting.md` — accepted. Read the
  counter-example section before touching the fill ledger.
- `docs/project-plan-v4.md` §6–§7 and §11–§12 — the capture and controller contracts, and their gates.
- `output/pdf/Trading_Engine_Orders_Trades_Credit_All_Cases.pdf` — glossary plus 18 worked cases and
  8 failure modes. **Partly stale:** written before the timestamp key, so its "credit is a pot"
  framing (cases A3/A4) holds only *within* one timestamp, and its E1 advice to "just count it" was
  wrong — that was a correctness hole, not an observability task.
- `output/pdf/Trading_Engine_Order_Trade_Merge_Deep_Dive.pdf` — problem framing and industry context
  (k-way merge, ITCH vs Bitstamp, event-time stream processing).
- `docs/decisions/0010-venue-selection.md` — why Bitstamp, and why the venue-neutral/adapter split.

## Commands to reverify this file

```bash
git log --oneline -5
cmake --build build --clean-first -j
ctest --test-dir build --output-on-failure
```

`cmake --build build` alone can report success against stale objects — this bit us once, and a green
run was reported against a library that did not match its own headers. Use `--clean-first` when the
answer matters.
