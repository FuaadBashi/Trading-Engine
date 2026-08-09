# Slice 1 — Recorder: remaining steps

Written 2026-08-08, after the raw capture path was proven against Bitstamp.

Everything below is offline work against a file on disk. No networking enters the C++ until
step 6. That ordering is the point of the slice, not a stylistic preference: it means every
bug you hit has exactly one possible cause.

---

## Status

| Step | State |
|---|---|
| 1 Raw capture script | done — `dump_raw_ws_bitstamp.py` |
| 2 Short probe and venue decision | done — see [ADR 0010](decisions/0010-venue-selection.md) |
| 3 Hour-long capture | **done — 252,374 events, 60.0 min, validated** |
| 4 C++ value types | **next** |
| 5 Binary record layer | not started |
| 6 Offline decoder | not started |
| 7 Golden round-trip test | not started |
| 8 Boost.Beast live recorder | not started |

All four Slice 1 ADRs (0004, 0005, 0006, 0010) are `accepted`. Steps 4 onward are unblocked.

---

## The corpus you are building against

`data/raw/btcusd-live-orders.jsonl` — 252,374 events, 121 MB, 3,599.3 s, 70.1 events/s.
Gitignored; provenance is in `data/captures.manifest.json`, which is committed.

```
sha256  c650e8e0ba3bdd8e62c6a61bc7bfb422a31be51d5609c5710b1270bf886059bc
```

Two known imperfections. Both are recorded in the manifest and neither is a reason to recapture —
they are the only real fault data you have, and Slice 2's fail-closed path needs them.

| | Where | Effect |
|---|---|---|
| Warm-up hole | lines 1–1,592 | Snapshot predates first event by 0.608 s; 11 deletes reference unknown orders. Book not valid until after this region. |
| Chain break | line 162,871 | One event lost (mod 4). Splits the file into two segments; segment 2 has no snapshot, so it is stream-usable but not book-replayable. |

Verify at any time with:

```
python scripts/validate_capture.py data/raw/btcusd-live-orders.jsonl
```

Back it up outside iCloud. It is not reproducible.

---

## Step 4 — `include/te/core/types.hpp` and `include/te/feed/events.hpp`

Declarations first, then the test, then the body.

**`types.hpp`** — `Price` (int64 ticks), `Qty` (int64, scale 1e-8), `OrderId` (uint64), `Side`,
`Instrument`. Strong types, not aliases: a `Price` that implicitly converts to `Qty` will
eventually be passed in the wrong argument position and compile cleanly.

**`events.hpp`** — the normalised internal event, which is *not* the venue's wire format. Bitstamp
emits `order_created` / `order_changed` / `order_deleted`; Coinbase emits `received` / `open` /
`done` / `match` / `change`. The `Feed` layer's job is to collapse both into one internal vocabulary.
Designing this against one venue is how the abstraction leaks — sketch how a Coinbase event maps
into it before you settle the shape.

Every event struct must be trivially copyable and fixed size. Assert it:
`static_assert(std::is_trivially_copyable_v<Event>)`.

**Unblocked:** [ADR 0004](decisions/0004-price-representation.md) and
[ADR 0005](decisions/0005-order-id-representation.md) are both accepted, and they already fix
your answers. Implement what they say rather than re-deciding:

- `Price` — signed int64, scale from `InstrumentSpec`, not a global constant. Bitstamp BTC/USD
  is 0.01 USD per tick.
- `Qty` — signed int64, 1e-8 BTC per unit.
- `OrderId` — strong type wrapping one `uint64_t`.
- Money/notional multiplication goes through a checked 128-bit intermediate.
- Decoding fails, rather than rounds, if a price is not a multiple of the declared tick size.

**Definition of done:** `test_types.cpp` passes, including
`"65168.69" -> Price{6'516'869}` exactly as ADR 0004 specifies, a case that would fail under a
`double` representation, a notional product that overflows 64 bits but not 128, and a
non-multiple-of-tick price that is rejected.

---

## Step 5 — `byte_buffer.hpp` and `record.hpp`

**`byte_buffer.hpp`** — read/write primitives over a byte span. Bounds-checked in debug.
Endianness stated explicitly, not inherited from whatever the host happens to be.

**`record.hpp`** — the fixed-size, versioned on-disk record. A version field in the header is
cheap now and impossible to retrofit once you have a corpus you cannot regenerate.
`static_assert(sizeof(Record) == N)` — this is the assertion that stops a silently-inserted
padding byte from invalidating every file you have recorded.

**Decide before writing:** does the REST snapshot go in the same binary file as the event stream
or a separate one? The capture script currently keeps them separate, and the reasoning is in the
script's docstring — but that decision was made for the JSONL layer and does not automatically
carry to the binary layer.

**Definition of done:** `test_byte_buffer.cpp` and `test_record_roundtrip.cpp` pass, including a
deliberate check that a truncated buffer is rejected rather than read past.

---

## Step 6 — offline decoder

Add simdjson (per [ADR 0001](decisions/0001-dependency-management.md)). Read the captured JSONL
line by line. No sockets.

Three things this step must get right, all of them found in the capture and all of them silent
if wrong:

- **Read `price_str`, `amount_str`, `id_str` only.** The bare `price`, `amount` and `id` are JSON
  numbers, i.e. doubles. See ADR 0004. Make the wrong field hard to reach, not merely discouraged.
- **Handle the long line.** The Coinbase L2 snapshot arrives as a single multi-megabyte frame; a
  fixed-size line buffer will truncate it. Bitstamp's snapshot is in a separate file, but the
  secondary Coinbase feed still has this shape.
- **Chain-check `event_id` / `pre_event_id`** per [ADR 0006](decisions/0006-sequence-gap-handling.md),
  and distinguish a real gap from a `bts:request_reconnect` and from the startup snapshot boundary.

**Definition of done:** `test_decoder.cpp` decodes a committed fixture — a handful of real lines
copied out of the capture into `tests/fixtures/` — and asserts exact integer values. Also assert
that the first line of a capture, `bts:subscription_succeeded`, is handled rather than crashing
the decoder, since it is not an order event at all.

Pull the fixture from the real corpus so the cases are not invented:

```
sed -n '1p;2p;3p' data/raw/btcusd-live-orders.jsonl  > tests/fixtures/bitstamp_head.jsonl
sed -n '162869,162872p' data/raw/btcusd-live-orders.jsonl > tests/fixtures/bitstamp_chain_break.jsonl
```

The second file is the real gap at line 162,871. A decoder that passes on clean data and silently
crosses that boundary has not implemented ADR 0006, and this fixture is what proves it.

Cross-check the C++ output against `scripts/validate_capture.py` on the full file: same event
counts, same single break at the same line. Two independent implementations agreeing is evidence;
one implementation agreeing with itself is not.

---

## Step 7 — golden round-trip test

1000 real lines → decode → write binary → read back → re-encode → compare byte-for-byte.

This is the test that makes the binary format trustworthy, and it is the reason `record.hpp` is
versioned. Store the fixture in `tests/golden/`.

**Definition of done:** round trip is byte-identical, and you have watched the test fail — flip a
field width or drop a `static_assert` and confirm it catches the change. A golden test you have
never seen fail is not evidence of anything. This is the "observed failing when appropriate" gate
from the roadmap's definition of done.

---

## Step 8 — Boost.Beast live recorder

Only now. By this point the decoder and the storage layer have known, tested behaviour, so any
new bug is unambiguously in the networking.

**Definition of done:** the C++ recorder produces a binary file that the step 7 golden test
accepts, from a live socket, running unattended without leaking memory under ASan.

---

## Slice exit criteria

From the roadmap's definition-of-done gates:

- Clean configure and build on the supported toolchains
- Unit and golden tests pass, and have been observed failing when appropriate
- Tests run under ASan and UBSan
- CI green from a clean checkout
- ~~ADRs 0004, 0005, 0006, 0010 have Decision sections that are not TODO~~ — done, all accepted
- You can re-derive the record layout and the gap-detection rule without opening the editor
- Learning-notes PDF written from the mistakes actually hit, not from generic course notes

On that last one, the mistakes this slice actually produced — worth writing from, because they
are specific and yours rather than generic:

1. Assuming a venue's documented capability without probing it (Coinbase `full`).
2. A client-side frame limit read as a venue rejection (`1009`, `max_size`).
3. `KeyboardInterrupt` escaping `asyncio.run()` and discarding a buffered file (`Task was
   destroyed but it is pending`).
4. Subscribing to a socket without draining it, and losing the window anyway.
5. A venue sending every number twice, once in a lossy representation.

---

## Open questions carried into Slice 2

- Does the internal event vocabulary survive contact with a second venue, or does adding the
  Coinbase adapter force a redesign? Cheaper to find out now than in week 11.
- Is one instrument enough, or does tick size need to be per-instrument from the start?
  [ADR 0004](decisions/0004-price-representation.md) assumes per-instrument; nothing tests it yet.
