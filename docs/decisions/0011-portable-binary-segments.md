# ADR 0011: Portable binary segments and snapshots

- **Status:** accepted
- **Date:** 2026-08-16
- **Slice:** 1-2 boundary

## Context

The current v2 `Sink` writes the 56-byte in-memory representation of `Record`. This is valid for a
trivially-copyable object read by a compatible build, and zeroed padding makes the current tests
repeatable. It is not a permanent file contract: padding, field layout and byte order still depend
on the compiler, ABI and host architecture.

The corpus is meant to outlive one executable and support replay, validation, plotting and later
format upgrades. Raw captures already use snapshot-backed segments with a manifest, so the binary
format should mirror those same continuity boundaries instead of inventing a second model.

## Options considered

1. **Continue writing the C++ object representation** — simplest and fast, but host-dependent and
   unsafe as a long-term exchange format.
2. **Use an external serialization framework** — mature evolution support, but adds a large concept
   and dependency before this project has learned and specified its own small format.
3. **Define a small explicit little-endian format** — more code and golden bytes to maintain, but
   portable, inspectable and directly exercises the byte-buffer skills this project intends to
   teach.

## Decision

The current v1/v2 object-layout files become a **legacy local format**. Keep a reader for committed
fixtures, but future durable captures use an explicitly encoded v3 segment format.

### General rules

- All multi-byte integers are little-endian.
- Prices and quantities are signed 64-bit integer ticks/units; floating point is forbidden.
- Order IDs and timestamps are unsigned 64-bit integers.
- Enum values are encoded as explicit unsigned bytes; their numeric meanings are part of the
  schema.
- Reserved bytes are written as zero and rejected if a future version gives them incompatible
  meaning.
- Readers validate magic, major version, header size, record size, scales and enum ranges before
  decoding records.
- A new incompatible layout creates a new major version and decoder. It never changes old bytes in
  place.

### Binary segment file

Each raw segment produces one binary event segment. The file begins with a 128-byte explicitly
encoded header containing:

- an eight-byte magic value identifying a trading-engine segment;
- major/minor format version;
- header and record byte sizes;
- an endian marker;
- zeroed flags/reserved fields;
- fixed zero-padded venue and instrument identifiers;
- price and quantity decimal scales;
- the bound snapshot microtimestamp;
- the local creation timestamp;
- the SHA-256 of the source snapshot;
- reserved space for compatible additions.

The v3 order-event record is exactly 64 bytes:

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 1 | record kind |
| 1 | 1 | record schema version |
| 2 | 1 | side |
| 3 | 1 | event kind |
| 4 | 4 | flags, initially zero |
| 8 | 8 | venue timestamp in microseconds |
| 16 | 8 | local receipt timestamp in microseconds |
| 24 | 8 | order ID |
| 32 | 8 | signed price ticks |
| 40 | 8 | signed quantity units |
| 48 | 16 | reserved, all zero in v3 |

Future trade records use another record kind with a separately documented 64-byte payload. A
reader never interprets one record kind as another.

### Binary snapshot file

The starting snapshot is encoded separately and bound to the event segment through the manifest
and hashes. It uses the same 128-byte header principles and fixed 32-byte order rows:

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 8 | order ID |
| 8 | 8 | signed price ticks |
| 16 | 8 | signed quantity units |
| 24 | 1 | side |
| 25 | 1 | priority provenance: unknown or observed |
| 26 | 6 | reserved, all zero |

The snapshot timestamp belongs in the header, not repeated in every order. Snapshot row order is
preserved for deterministic replay but the provenance byte prevents it being mistaken for proven
FIFO priority.

### Manifest and integrity

The manifest is the authoritative run/segment index. It records raw and binary filenames,
SHA-256 hashes, byte sizes, counts, first/last event IDs, snapshot timestamp, start/end reason,
chain status and gap diagnostics. It is replaced atomically only after files have been flushed and
closed.

No v3 file may be labelled complete unless its byte size matches its declared record count and all
record/header/hash checks pass.

### Open: whether v3 records carry `order_subtype`

Deferred deliberately, not overlooked. Bitstamp sends an `order_subtype` on every order message
that the decoder currently discards, so it reaches neither `OrderEvent` nor any record. Measured
across the available corpora:

| Corpus | order events | `order_subtype` values observed |
|---|---:|---|
| 29s reference segment | 1,433 | `5` (1,231), `4` (178), `0` (24) |
| hour capture | 252,374 | `5` (225,830), `4` (22,075), `0` (4,288), `6` (177), `2` (4) |

Five distinct values, two of them rare enough (`6` at 0.07%, `2` at 0.002%) that their meaning
cannot be inferred from frequency alone. Nothing in the public Bitstamp documentation defines
them, so assigning them meaning now would be guessing.

The decision on whether v3 stores subtype is therefore blocked on evidence, specifically on
joining `live_orders` against `live_trades` so that each subtype can be checked against whether
its orders actually traded. Until then:

- `OrderEvent` stays unchanged, so the legacy 56-byte `Record` does not get to dictate the
  permanent domain model by accident, and no format bump is spent on an unvalidated field.
- Venue-specific classification that *can* be justified from the data today lives in
  `BitstampEventClassifier` (see `feed/bitstamp_classifier.hpp`), between the decoder and the
  book, rather than being pushed into `OrderEvent` or `OrderBook`.
- When the joined-data investigation resolves what subtype means, adding it is a v3 schema
  decision recorded here, not a silent widening of the in-memory event.

## Consequences

- Files are portable across compilers and little-/big-endian hosts when decoded according to the
  schema.
- The C++ `Record` layout can change for in-memory reasons without changing persistent bytes.
- Golden tests must assert exact documented bytes, not bytes obtained by writing the same struct and
  reading it back with the same build.
- The current v2 writer remains useful as a learning artefact and legacy fixture but does not meet
  the durable Slice 1 exit gate.
- Encoding costs more instructions than a whole-struct write; replay/storage performance must be
  measured before considering a separately versioned machine-local cache format.
- Snapshot and event data stay separately typed and explicitly bound rather than pretending a
  snapshot row is a live add event.

## How you would defend this in an interview

Trivial copyability makes copying object bytes legal, but it does not define a portable disk
format. I kept the original object-layout files readable as legacy data and introduced an explicit
little-endian schema with fixed headers, records, versioning and manifest hashes. That lets the
in-memory book evolve without silently changing the meaning of historical captures.
