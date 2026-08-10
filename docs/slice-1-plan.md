# Slice 1 - Data contract and recorder

**Updated:** 2026-08-09
**Project source of truth:** [project-plan-v2.md](project-plan-v2.md)

Slice 1 turns captured venue payloads into exact, normalized, versioned C++ events without
putting networking into the decoding loop. The order is deliberate: types, parsing, decoding and
storage become deterministic before Boost.Beast is introduced.

## Status

| Step | State |
|---|---|
| Venue probe and ADR 0010 | complete |
| Segmented Python order capture + validator | complete |
| Clean positive and real-gap negative corpora | complete |
| C++ value types | complete - behavior tests and header contracts pass |
| Normalized events | complete - storage test and header contracts pass |
| InstrumentSpec + exact decimal parser | **current - design and tests next** |
| Offline Bitstamp decoder | not started |
| Binary format + byte buffer | not started |
| Golden semantic/binary round trip | not started |
| Joined order+trade sample contract | not started |
| Boost.Beast live recorder | not started |

## Corpora

### Positive development corpus

```text
data/raw/bitstamp-btcusd-20260809T100421Z/
```

- 1,433 order events over 29.2 seconds;
- one valid snapshot-backed segment;
- no detected chain break;
- sufficient for a 1,000-event decoder/record fixture.

Validate with:

```bash
python scripts/validate_capture.py data/raw/bitstamp-btcusd-20260809T100421Z
```

### Negative/fault corpus

```text
data/rawOld/btcusd-live-orders.jsonl
data/rawOld/btcusd-live-orders.snapshot
```

- 252,374 order events over 3,599.3 seconds;
- real chain break at line 162,871;
- legacy startup alignment defect;
- useful for gap and failure tests, but not one continuous replayable book.

Its provenance and hashes are in `data/captures.manifest.json`.

## Step 1 - normalized events (complete)

The engine vocabulary must not expose Bitstamp strings. The first design maps:

```text
order_created -> add
order_changed -> modify
order_deleted -> remove
```

The deliberate missing-type failure was observed, the definitions were implemented, and the
focused build is green. The event contract is:

- `venue_timestamp_us` is Bitstamp venue time in microseconds, parsed from `microtimestamp`;
- `order_id`, `price`, `side` and `kind` describe the affected resting order;
- `quantity` is the resulting venue-reported resting quantity, never a delta:
  - `add`: quantity inserted into the book;
  - `modify`: new remaining quantity replacing the previous quantity;
  - `remove`: quantity reported at deletion, retained for audit only; removal is by `OrderId`;
- local receive time is separate ingestion metadata and must not be invented for payload-only
  historical captures;
- instrument scale and identity enter through the decoder's `InstrumentSpec` context in Step 2;
  embedding an instrument identifier in every event is revisited before multi-instrument replay.

Permanent structural requirements such as trivial copyability belong next to the completed type
as a header `static_assert` with a useful message. This is an in-memory queue/ownership contract,
not permission to serialize the object using `memcpy`.

Definition of done achieved:

- the deliberate missing-type build failure was observed;
- event storage behavior passes;
- event types are fixed-size and trivially copyable;
- field meanings and timestamp units are documented;
- permanent representation/copyability contracts live beside their types rather than in tests.

## Step 2 - InstrumentSpec and exact decimal parsing

Create a small isolated parsing unit before simdjson integration.

Required behavior:

- the instrument supplies price and quantity decimal scales;
- `"65168.69"` becomes exactly `Price{6'516'869}` for a two-decimal price scale;
- quantities use the observed eight-decimal BTC scale;
- no conversion passes through `double`;
- malformed input, excessive precision and overflow return explicit errors;
- at least one synthetic instrument uses a different scale to prove the value is not global.

Do not hide parsing inside constructors for `Price` or `Qty`. Those types represent already
validated engine values; venue parsing belongs at the boundary.

Definition of done:

- table-driven positive and negative tests;
- checked overflow behavior;
- no floating-point type in the price/quantity boundary API;
- InstrumentSpec scale is tested rather than assumed.

### Step 2 learning plan

Work in small red-green stages rather than writing the complete parser at once:

1. **Specify the boundary in English.** List the `InstrumentSpec` metadata and the parser errors a
   caller must distinguish.
2. **Design the smallest error result.** Implement only the `Result<T, Error>` behavior needed to
   return either an exact value or a reason; do not build a general-purpose library first.
3. **Write scale tests.** Prove BTC/USD uses two price decimals and eight quantity decimals, then
   add a synthetic instrument with different scales.
4. **Write happy-path parser tests.** Cover `"65168.69" -> 6'516'869`, whole numbers and missing
   trailing fractional zeroes.
5. **Implement the simple digit loop.** No `stod`, `strtod`, `double` or rounding.
6. **Add failure tests one category at a time.** Empty/malformed input, sign rules, multiple decimal
   points, excess precision and overflow.
7. **Wrap raw scaled integers as `Price` and `Qty`.** Keep generic decimal parsing separate from
   domain-specific validation such as rejecting negative quantity.
8. **Run normal and sanitizer builds.** Explain every overflow check before beginning simdjson.

## Step 3 - offline Bitstamp decoder

Add simdjson through ADR 0001 and decode committed real fixtures line by line. No sockets.

Required behavior:

- control events are recognized explicitly;
- `price_str`, `amount_str` and `id_str` are used;
- side and event-kind mappings are tested;
- missing/unknown/malformed fields produce reasoned errors;
- pure payload conversion is separated from segment chain state where practical;
- a real clean fixture yields the expected count and exact integer values;
- the real gap fixture fails at the expected transition.

JSON numbers are not inherently doubles: simdjson can parse integer tokens exactly. We still use
the documented string representation for the boundary policy and consistency checks. Never claim
that every JSON parser must decode all numbers as IEEE double.

## Step 4 - joined orders and trades contract

The eventual fill-validation claim needs trade/cancel classification. `live_orders` alone should
not be assumed sufficient without proving the semantics of `amount_traded`, order subtype and
deletion events.

Before research work:

- subscribe to Bitstamp `live_orders_btcusd` and `live_trades_btcusd`;
- record local receive timestamps and venue timestamps;
- preserve each channel payload separately or tag channel identity unambiguously;
- define how reconnect/gap boundaries apply to both streams;
- test joining a trade's order IDs to observed order lifecycle records;
- obtain and validate a short sample before collecting many hours.

This capture upgrade may be done after the current type/parser/decoder lesson. It must be complete
before fill-label construction, not before basic C++ work.

## Step 5 - binary format and byte buffer

Write the format specification before the structure.

Recommended shape:

- one versioned binary event file per continuous segment;
- explicit byte order and explicit field encoding;
- snapshot encoded separately or referenced from the segment header;
- run manifest records gap/reconnect/end reasons and hashes;
- order and trade record kinds are typed;
- no raw `memcpy(OrderEvent)` serialization.

`std::is_trivially_copyable_v<OrderEvent>` is useful for in-memory transport. It does not guarantee
portable padding, offsets, endianness or version compatibility.

Definition of done:

- bounds-checked byte reads/writes;
- truncated input rejected;
- stable header/version/type/length semantics;
- expected serialized size tested independently of `sizeof(OrderEvent)`;
- ADR 0006 amended if the selected binary boundary model differs from its current wording.

## Step 6 - golden evidence

Use:

```text
real JSON fixture -> normalized events -> binary -> normalized events
```

Assert:

- event counts match;
- decoded events compare semantically equal;
- binary output matches a committed expected byte fixture;
- corrupted version/type/length/checksum cases fail;
- the test has been deliberately observed failing.

Do not require byte-identical regenerated JSON. A normalized event does not retain source member
ordering, whitespace or irrelevant venue fields.

## Step 7 - Boost.Beast live recorder

Only after the offline decoder and binary writer have known behavior:

- TLS WebSocket connection and subscription;
- bounded frame handling;
- reconnect into a fresh snapshot-backed segment;
- no hot-path exception escape;
- output accepted by the same offline reader and validator;
- unattended sanitizer run with explicit message/drop counts.

## Slice 1 exit gate

- all Slice 1 tests pass under ASan/UBSan;
- Linux CI passes from a clean checkout;
- clean corpus and gap fixture produce expected independent Python/C++ results;
- binary bytes are versioned and stable;
- relevant ADRs contain no unresolved implementation contradiction;
- current status and commands are accurate in README;
- Slice 1 learning-notes PDF and quiz completed;
- tagged commit created only after the above.

## Immediate next action

In English, propose the fields of `InstrumentSpec` and the values of `ParseError`. Do not write the
parser body yet. After reviewing those names and responsibilities, create the first failing scale
and successful-conversion tests.
