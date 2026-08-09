# ADR 0005: Order ID representation

- **Status:** accepted
- **Date:** 2026-08-09
- **Slice:** 1

## Context

**Rewritten 2026-08-08.** The original premise — "Coinbase issues UUID strings, and a string
cannot live in a trivially copyable fixed-size event struct" — is void. The venue is now Bitstamp
(see [ADR 0010](0010-venue-selection.md)), which issues numeric order IDs. The hard problem this
ADR was written to solve does not exist on this venue.

What the capture actually shows, per `order_created` event:

```
"id": 2037293133250560,  "id_str": "2037293133250560"
```

Sixteen digits, comfortably inside `uint64`. Two things are worth noticing before deciding:

- The ID is sent **twice**, once as a JSON integer and once as a string. JSON itself does not
  require a parser to use IEEE `double`, and simdjson can read an integer directly as
  `uint64_t`. However, many generic JSON pipelines do coerce numbers to `double`, where exact
  integer representation ends above 2^53. Reading `id_str` gives this decoder one explicit,
  portable representation and lets it reject malformed or overflowing IDs itself.
- Bitstamp also emits `event_id` and `pre_event_id`, which are UUID-shaped and are a *different*
  identifier serving a different purpose (event chaining, see [ADR 0006](0006-sequence-gap-handling.md)).
  Do not conflate them with order identity.

The original UUID-interning question is still live for a second venue: if Bitfinex or a Coinbase
adapter is added later, a UUID-issuing venue reappears and the `Feed` layer must normalise.

## Options considered

1. **Parse `id_str` into `uint64`** — exact, fixed size, no collisions, no table. Requires the
   decoder to read the string field and reject the number field, and requires a decision about
   what to do if a venue ever issues an ID that does not fit.
2. **Parse the bare numeric `id` as `uint64_t`** — exact with simdjson while the literal fits,
   and avoids decimal-text conversion. It is safe in this decoder, but easier to break if the
   payload later passes through a component that represents every JSON number as `double`.
3. **Intern into a dense engine-local integer** — decouples internal IDs from any venue's format,
   which is what makes a multi-venue `Feed` clean. Costs a hash lookup per event on the hot path
   and a table that grows with order count.
4. **Fixed 16-byte raw storage** — venue-agnostic including UUIDs, but doubles the ID field width
   in every event and record, and makes comparison slower.

## Decision

Represent a Bitstamp order ID as a strong `OrderId` containing one `std::uint64_t` value.
The Bitstamp decoder reads `id_str` and performs checked decimal-to-`uint64_t` conversion;
malformed text and overflow are decoding errors. Tests also verify that `id` and
`id_str` agree when both are present, so a venue schema inconsistency is detected early.

This is the version-1 engine and record representation, not a claim that every venue uses
64-bit numeric IDs. A future adapter with UUID or string IDs must provide a collision-free,
persisted mapping into the engine ID domain or introduce a new versioned record format. It
must not hash identifiers into 64 bits and silently accept collision risk.

## Consequences

- Comparison, hashing and fixed-size storage are cheap, exact and allocation-free.
- The on-disk version-1 record dedicates eight bytes to `OrderId`; layout tests and
  `static_assert` make that contract visible.
- Decoder work includes checked string parsing and a consistency check against the numeric
  field, rather than trusting whichever representation is easiest to access.
- `event_id` and `pre_event_id` remain separate event-chain identifiers and are never stored
  in `OrderId`.
- Supporting a venue with identifiers wider than 64 bits requires an explicit adapter mapping
  plus persisted mapping data, or a record-format version change. The current choice does not
  pretend to be universally venue-neutral.

## How you would defend this in an interview

Bitstamp's captured order IDs are unsigned decimal integers that fit in 64 bits, so `uint64_t`
is exact and keeps event records compact. I deliberately parse `id_str` with overflow checks
and cross-check the duplicate numeric field; this avoids accidental precision loss in JSON
systems that coerce numbers to `double`. I keep event-chain IDs separate and require an
explicit persisted mapping or format version for a future UUID-based venue.
