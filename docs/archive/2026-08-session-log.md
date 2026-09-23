# Session log — 2026-08-22 to 2026-08-28

What changed, and more usefully **why**, including the wrong turns. `status.md` says what is true
now; ADR 0013 records the decision. This file keeps the reasoning that neither of those preserves —
several conclusions here were reached by being wrong first, and the wrongness is the useful part.

Commits: `9243aaa` … `ad6e9dc`. Tests went 158 → **201**, plus 8 Python.

---

## 1. The double-counted fill

**Symptom.** Replaying the first real joined capture returned `unexpected_order_apply_failure`.
Nine order events failed with `unknown_order_id` across five orders.

**Method that mattered.** The first instinct was to trace one failing order and fix what it showed.
That was checked instead: all nine were traced, and all nine turned out to be the same cause. Had
they not been, fixing one would have dropped the count and looked like progress while leaving a
second bug live.

**Cause.** Bitstamp announces every fill **twice** — once on `live_orders` (whose `amount` is
already post-fill, with `amount_traded` naming the fill) and once on `live_trades`. The reconciler
treated the trade as fresh news and subtracted the same fill again, deleting orders that still had
quantity resting.

**Wrong turn #1: `amount_traded` is cumulative.** It is not. Measured across the capture:
per-event holds 156/156, cumulative holds 111/156 — and the 111 are exactly the events that happen
to be an order's *first* fill. A small sample would have confirmed the wrong reading.

**Wrong turn #2: fix it with the tie-break.** Tempting and wrong. Two cases pull opposite ways:

- `order_changed` sharing a timestamp with its trade → order-first double-counts
- `order_created` sharing a timestamp with the trade that consumes it → trade-first leaves the
  order resting forever, because `reconcile` runs before `observe` has seen it

No fixed ordering is correct for both. The correction itself had to become idempotent.

**Wrong turn #3 (mine, caught by the user): one running credit per order.** The counter-example:

```
start                         book 10   credit  0
trade-only fill of 6 at T1    book  4   credit -6     (correct)
order_changed at T2 traded 1  book  3   credit -5
matching trade of 1 at T2               credit still negative -> corrects again
```

The book should hold 3. A debt from T1 contaminated an unrelated fill at T2. The fix is to scope
each credit to `(orderId, venueTimestampMicros)`. Verified before adopting: 144 of 146 buckets
balance to exactly zero, and **zero** fills have a matching trade at a different timestamp.

**Result.** 9 failures → 0. Checkpoint comparison: **0 mismatches of 4,533 levels**.

---

## 2. Three counters that measured the wrong thing

**`ordersRemovedWithOpenFillBalance` read 21 on a capture with zero residuals.** A health counter
reading 21 on a clean run is not measuring health. All 21 were orders whose fill-carrying
`order_changed` shares a timestamp with its `order_deleted`: order events win exact ties, so the
delete always runs before that microsecond's trades and the credit never gets a chance to clear.
Structural, not a fault. Redefined as `ordersRemovedWithUnmatchedFill`, restricted to credits from
an *earlier* timestamp. Now reads 0.

**14.3% of input events were in no counter at all.** Events outside the seed→cutoff window vanished
silently — 4,208 of 29,404. Plan v4 §12 requires every input to be accounted for. Now
`beforeSeed + read + afterCutoff == input size`, asserted on the real capture.

**Lesson worth keeping:** a counter that reads non-zero on a healthy run, or that omits inputs, is
worse than no counter — it looks like evidence.

---

## 3. Where the plan turned out to be wrong

**Plan v4 §6 declared the replay key as `(venueTimestampMicros, captureOrdinal)`.** The ordinal was
plumbed through and then measured *before* adopting it:

| | Shared timestamps | Ordinal puts the trade first |
|---|---|---|
| 4 segments | 427 | **394 (92%)** |

Bitstamp's trade frame reaches the socket before its matching order frame almost every time.
Ordering by ordinal runs `reconcile` before `observe`. Both policies reach the same final book, but
the ordinal key produces **41 apply failures** where the current rule produces none — the books only
agree because each spurious correction is cancelled by the raw event then failing as redundant.
`Replay` aborts on the first failure, so the plan's key does not work.

§6 was amended. The code was not. `captureOrdinal` is still carried as provenance — and carrying it
is the only reason the measurement was possible.

---

## 4. Two capture bugs the C++ side surfaced

Replaying the second capture failed. Not a replay bug:

**Reseed served a stale seed.** After a chain-gap reconnect, the snapshot's microtimestamp
*predated* the segment's first captured event by 378 ms — a window covered by neither source, hence
seven removes for orders nobody had seen. Subscribing before snapshotting (which the script already
did) is necessary but not sufficient, because Bitstamp can serve a snapshot describing the book as
it was several hundred milliseconds earlier. The script now refetches until
`snapshot.microtimestamp >= first captured order event`, and ends the segment as
`snapshot_never_overlapped_stream` rather than emitting a holed seed.

**The manifest named a file that was never written.** A gap-terminated segment has no checkpoint,
but the manifest advertised one, so `loadJoinedCapture` failed the whole capture. Paths are now
recorded only when the file exists.

Both were found because the C++ pipeline **refused to accept bad data** rather than limping on.

While fixing this I also broke and then repaired something instructive: the restructured control
flow let a trailing `else` overwrite specific seeding-failure reasons with a generic
`subscription_timeout` — the same reason-clobbering this project keeps eliminating.

---

## 5. The finding that changed the roadmap

`correctionsGenerated` is **0** on every real capture. That is not sample size:

| | Trades naming a resting order | Covered by a `live_orders` credit | Uncovered |
|---|---|---|---|
| 1,059s, 753 trades | 637 | 637 | **0** |

**637 of 637.** Bitstamp reports every fill against a resting order. A correction can only fire on
an uncovered fill, so `TradeReconciler` has never fired on real venue data.

**Wrong turn #4:** the first detector for this asked "traded but never deleted", and reported one
hit. Tracing it showed the order was partially filled and *still resting* — entirely normal. The
detector was rewritten to ask the question that actually matters: is this trade's quantity already
covered by a credit at the same timestamp? Answer: always.

Consequences: the reconciler is insurance rather than a validated path; the search for a real
example was called off; and the long-standing explanation for the three hardcoded adjustments in
`test_golden_replay.cpp` ("silent full fills") became unlikely. Those three are now recorded as
**unexplained** rather than explained-but-unfixed.

---

## 6. Making a green build mean something

A fresh checkout reported **194/194 green while silently skipping six tests**, including the
correctness gate. Every real-data test depended on gitignored captures.

`tests/fixtures/joined-capture-golden/` (2.9 KB, committed, generated by
`scripts/make_golden_fixture.py`) fixes that. Two design points:

- **The checkpoint is hand-derived** from what the venue would report, order by order. A fixture
  whose expected state came from replaying the book under test would only prove the code agrees
  with itself.
- **It covers what real data cannot.** Order 102 is consumed by a trade `live_orders` never
  reports, so the correction path is exercised end to end through the real loader — the case
  1,059 seconds of capture never produced.

The remaining data-dependent tests now skip *loudly*, naming the evidence not run.

---

## 7. Two risks that were never tested

**Populated `OrderBook` move.** `Replay` moves a populated book twice per replay, and `orderIndex_`
holds list iterators into the book's own levels. Nothing tested it — the `static_assert`s only prove
copy is deleted and move exists, not that move is *correct*. The tests deliberately mutate through
inherited locators, because a read-only check would pass even with every iterator dangling
(`qtyAt` never touches `orderIndex_`). Defaulted move turned out correct; now it is proven.

**No independent oracle.** `test_book_oracle.cpp` compares `OrderBook` against a deliberately naive
model — one flat hash map, level totals summed on demand, no locators or lists — over 28,000
generated events. Agreement is evidence only because the two share no code or data structure.

Checked that it is not vacuous: 2,468 accepted mutations, up to 8 live levels, five of six
`ApplyError` kinds provoked. The sixth (`level_quantity_overflow`) is documented as out of scope —
the oracle sums in plain `int64`, so at those magnitudes it would overflow too.

---

## What a future session should take from this

1. **Measure before adopting, including your own plan.** Plan v4 §6 was wrong; only carrying
   `captureOrdinal` and testing both policies revealed it.
2. **A counter that reads non-zero on a healthy run is not a health counter.**
3. **Trace every instance, not the first one.** Same error message ≠ same cause.
4. **Green is not evidence unless it runs on a clean checkout.**
5. **When a fix looks like it needs an ordering rule, check whether the operation should be
   idempotent instead.** That was the whole shape of ADR 0013.
