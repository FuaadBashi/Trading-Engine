# Status handoff

Written for whichever Claude session picks this project up next. Built by reading the repo at `HEAD`
(`ad6e9dc`, 2026-08-28) and running the tests, not from memory of any conversation.

`docs/project-plan-v4.md` is the authoritative long-range plan. This file is the shorter "what is
actually true right now" companion, because v4's own baseline table (§2) predates most of the work
below.

Verify before trusting: re-run the commands at the bottom.

## Build state

Clean build from scratch, **201/201 CTest cases pass**, plus 8 Python tests (Apple Clang, default
toolchain). The CI dual GCC/Clang + ASan/UBSan matrix has not been re-verified in this pass.

**A fresh checkout is now meaningful.** It used to report every test green while silently skipping
the entire real pipeline. `BitstampJoinedCapture.GoldenFixtureReplaysToHandWrittenCheckpoint` runs
from a committed 2.9 KB fixture and never skips, so plan v4 §10's gate — "a fresh checkout runs
every mandatory test without private local files" — is met. Tests that still need private captures
now say what evidence is missing instead of just naming a path.

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
| Mandatory golden fixture | `tests/fixtures/joined-capture-golden/`, built by `scripts/make_golden_fixture.py` | runs on any checkout; covers the correction path real data never reaches |
| Populated-book move safety | `test_order_book.cpp` | move-construct, move-assign and double-move a populated book, then mutate through inherited locators |
| Independent book oracle | `test_book_oracle.cpp` | 28,000 generated events compared against a separately written model |
| Capture discovery and loading | `capture/manifest_reader.cpp`, `capture/segment_loader.cpp` | manifest paths stay separate from payload/frame joining; the loader decodes into `CapturedOrderEvent` |
| Capture replay coordination | `capture/capture_coordinator.cpp` | loads each described segment, replays it, and compares against an optional checkpoint |
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
and `OrderBook`**. It says nothing about corrections firing when they should.

That path is now covered end to end by the committed golden fixture, which contains a resting order
consumed by a trade `live_orders` never reports, and asserts exactly one correction is manufactured.
That is synthetic evidence through the real loader and merge loop — stronger than unit tests alone,
weaker than seeing it on the wire.

**And it may not be reachable on this venue.** Across 1,059 seconds of joined capture and 753 trades,
**637 of 637** trades against a resting order had their fill already reported by `live_orders` via
`amount_traded`. Zero uncovered. A correction can only fire on an uncovered fill, so the path has
never been reached on real Bitstamp data — see ADR 0013, "The correction path has not been reached on
real data", for the per-segment numbers.

Keep the reconciler: it is correct, exercised end to end by the golden fixture, and other venues do
not all report post-fill quantities. But it has never fired on real venue data, so do not claim it
is validated *against Bitstamp*, and do not go capturing indefinitely in the hope of catching one.

That also unsettles the explanation for the three hardcoded adjustments in `test_golden_replay.cpp`.
They were assumed to be orders fully filled with no `order_deleted`; if `live_orders` reports every
fill, that is unlikely. They are now **unexplained**, and cannot be diagnosed from that fixture —
it is order-only and the window is historical. Open candidates: `order_subtype` semantics,
liquidations, a venue-side move between levels.

## v3 tape segments — state as of 2026-09-01

v3 is a **derived accelerator**, not the archive: raw captures are archival truth and must not be
deleted. Decision in ADR 0011; bytes in `docs/specs/v3-segment-format.md`, which is authoritative and
replaced the drifted layout tables that used to live in the ADR.

Built and tested: the header codec (now stamping an ordering-policy version), the event record
codec, `EventSegmentWriter`/`EventSegmentReader`, `MergeCursor`, and `writeEventTape`, which turns a
`JoinedCapture` into an L1 tape. `te::bitstamp::Replay` now drives `MergeCursor` too, so ADR 0013's
tie-break exists in exactly one place; the real 29k-event corpus test still passes with zero
residuals after that refactor.

**Nothing replays *from* a tape yet, and closing that loop is deliberately deferred (2026-09-02).**
Raw replay already works, so a faster path buys nothing measurable today. Pick this back up when one
of these triggers fires:

- replay time over the corpus becomes an actual bottleneck you have measured, not assumed;
- Stage 8 begins, and reference-versus-optimized comparison needs a fixed event tape as its input;
- a capture grows large enough that re-decoding JSON on every run is the slow part.

`EventSegmentReader` decodes records; no path feeds them into an `OrderBook`. Until that exists the
tape is write-only, and these remain open:

1. **Classifier warm-up blocks equivalence.** A tape holds only `(seed, cutoff]`, but `Replay` warms
   a stateful classifier on pre-seed orders. A tape therefore cannot yet claim to replay identically
   to its raw capture — the property Stage 8 needs. Four options, cheapest first, in the spec under
   "Known gap: classifier warm-up". Start with measuring whether classifier state actually crosses
   the seed boundary; if it does not, this closes with evidence and no code.
2. **Lineage is unbound.** `CaptureManifest` carries `formatVersion`, venue, instrument and paths —
   no hashes, no `runId`. A derived artifact that cannot name its source is not provenance-bound.
   Plan v4 §8 already specifies the fields.
3. **Snapshot rows unimplemented.** The 32-byte record size is accepted by the header codec but no
   codec exists.
4. **Deferred structural cleanups, both agreed 2026-09-01, neither done.** Split `telemetry/` by
   format generation so legacy v2 (`record`, `sink`, `recorder`) is clearly separated from v3, as
   ADR 0011 requires. Label the empty placeholder headers under `include/te/engine/` and
   `include/te/venue/` with the plan section that owns them. Both were kept out of the tape work
   deliberately: mixing a structural move into a behavioural change destroys the ability to review
   or revert either one.

## Open work

**Highest value first:**

1. **Amend plan v4 §6.** It declares the replay key as `(venueTimestampMicros, captureOrdinal)`.
   The ordinal is now decoded and carried, and measurement says the plan is wrong for this venue:
   Bitstamp's trade frame arrives before its matching order frame in **394 of 427** shared
   timestamps, so ordering by ordinal runs `reconcile` before `observe` and produces 41 apply
   failures on the reference capture. The code keeps order-wins-tie deliberately; the plan is what
   needs changing. See ADR 0013, "`captureOrdinal` is carried, but is deliberately NOT the
   tie-break".

2. **Rest of Stage 0 cleanup (v4 §10)** — structural-vs-decision-ready validation split, CI guard
   activation. Closed since this was last written: the legacy `receipt_timestamp_us` naming bug
   (was `Nanos` typed, named like microseconds; renamed to `receipt_timestamp_ns`, 2026-09-02) and
   the mandatory small fixture (`tests/fixtures/joined-capture-golden/`, committed and asserted
   mandatory by `BitstampJoinedCapture.GoldenFixtureReplaysToHandWrittenCheckpoint`).

**Deliberately deferred, documented as such in ADR 0013:** the book health state machine
(`unseeded → warming → valid → stale_or_gapped → resyncing → valid`) and any reorder window. After a
gap a quantity decrease may reflect fills that were never observed, and no policy covers that. It
needs its own decision and its own evidence — do not guess at it.

**Partly addressed since:** the *capture* side of gap/reseed handling now has a rule — a segment's
seed snapshot must not predate its first captured event, enforced by refetch and by ending the
segment as `snapshot_never_overlapped_stream` rather than emitting a holed seed. The *replay* side
(what the engine should do when it meets a gap) is still undecided.

## Where to read more

- `docs/handoff/2026-08-session-log.md` — how this state was reached, including the wrong turns and
  the measurements that overturned them. Read it before revisiting any decision below; several were
  reached by being wrong first.

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
