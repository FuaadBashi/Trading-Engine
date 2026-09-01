# Specification: v3 portable segment format

- **Status:** living specification — this file is authoritative for bytes
- **Decision record:** `docs/decisions/0011-portable-binary-segments.md` (why; do not restate here)
- **Implemented by:** `segment_format.{hpp,cpp}`, `event_record_format.{hpp,cpp}`
- **Last verified against code:** 2026-09-01

All integers little-endian. Reserved bytes are written as zero and **rejected if nonzero** — a
reader fails closed rather than skipping unknown data.

## Segment header — 128 bytes

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 8 | magic `TESEG\0\0\0` |
| 8 | 2 | major version (3) |
| 10 | 2 | minor version (1) |
| 12 | 2 | header size (128) |
| 14 | 2 | record size (32 snapshot, 64 event) |
| 16 | 4 | endian marker `0x01020304` |
| 20 | 4 | flags (must be 0) |
| 24 | 16 | venue id, ASCII, zero-padded |
| 40 | 16 | instrument id, ASCII, zero-padded |
| 56 | 1 | price decimal scale (≤ 18) |
| 57 | 1 | quantity decimal scale (≤ 18) |
| 58 | 2 | ordering policy version |
| 60 | 4 | reserved |
| 64 | 8 | seed snapshot timestamp, µs |
| 72 | 8 | creation timestamp, µs |
| 80 | 32 | seed snapshot SHA-256 |
| 112 | 16 | reserved |

Free for future use: **20 bytes** (offsets 60–63, 112–127).

### Ordering policy version

| Value | Meaning |
|---:|---|
| 0 | segment claims no ordering policy — snapshots, or a transcription of arrival order |
| 1 | merged by venue timestamp, exact ties resolved in favour of the order event (ADR 0013) |

A tape stores records in the order a merge policy produced, so the policy is part of what the bytes
mean. Both the encoder and the decoder reject a value above the highest known policy: a reader that
cannot reproduce the ordering refuses the file rather than trusting a sequence it cannot verify.
Because segments are derived and regenerable, changing the policy means regenerating tapes, not
migrating an archive.

## Event record — 64 bytes

Bytes 0–15 are common to every record kind.

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 1 | record kind: 1 = order, 2 = trade |
| 1 | 7 | reserved |
| 8 | 8 | venue timestamp, µs |

### Kind 1 — order

| Offset | Bytes | Field |
|---:|---:|---|
| 16 | 8 | order id |
| 24 | 8 | price, signed ticks |
| 32 | 8 | quantity, signed units (resting quantity, not a delta) |
| 40 | 8 | amount traded, signed units (this event's fill; see ADR 0013) |
| 48 | 1 | side |
| 49 | 1 | event kind |
| 50 | 14 | reserved |

Free for future use: **21 bytes** (offsets 1–7, 50–63).

### Kind 2 — trade

| Offset | Bytes | Field |
|---:|---:|---|
| 16 | 8 | buy order id |
| 24 | 8 | sell order id |
| 32 | 8 | quantity, signed units |
| 40 | 24 | reserved |

Free for future use: **31 bytes** (offsets 1–7, 40–63).

## Snapshot rows — 32 bytes

Declared in ADR 0011 and accepted by the header's record-size check. **No codec is implemented
yet.** Specify the row layout here when one is written.

## Reader validation order

Magic → major version → header size → record size → endian marker → flags → venue → instrument →
decimal scales → reserved bytes. Records are validated for kind, enum ranges and reserved bytes
before any field is trusted.

## Tape level: what is precomputed

A tape written by `writeEventTape` is **L1**. Only the merge is precomputed; everything downstream
still runs on read.

| Level | Contents | Baked into bytes | Reader still runs |
|---|---|---|---|
| **L1 — current** | order + trade records, merged | ordering | classifier, book, reconciler |
| L2 | L1 minus classifier-rejected events | ordering + classifier | book, reconciler |
| L3 | only what reached the book, corrections included | ordering + classifier + reconciler | book |

L1 was chosen because the reconciler's correction path has never fired on real data (ADR 0013), so
L3 would freeze an unexercised policy into stored bytes. L3 is also what plan v4 §18 implies for
reference-versus-optimized benchmarking; derive it from L1 when Stage 8 needs it, rather than
guessing its requirements now. Note that under L3 the trade record kind is never written.

## Known gap: classifier warm-up

**A tape is not yet proven to replay identically to the raw capture it came from.**

`te::bitstamp::Replay` feeds pre-seed order events to a *stateful* `EventClassifier` (price-zero
lifecycles) before the replay window opens. A tape stores only `(seed, cutoff]`, so replaying one
starts with a cold classifier and may classify differently.

This blocks the identical-hash claim Stage 8 depends on. Options, cheapest first:

1. **Measure whether it matters.** If classifier state never survives the seed boundary in practice,
   the gap closes with evidence and no code. Testable against the existing corpus.
2. **Carry warm-up records.** Write pre-seed order events too; a reader treats records at or before
   `seedTimestampMicros` as classifier-only and never applies them to the book. Self-sufficient tape,
   costs bytes.
3. **Persist classifier state** in the header or a side-car.
4. **Move to L2**, removing the classifier from the read path entirely.

Do not claim tape/raw replay equivalence until one of these lands.

## Not carried by this format

`captureOrdinal`, `localWallTimestampNanos`, `localSteadyTimestampNanos`, `runId` and `segmentId`
exist in raw captures but are **not** stored in v3 segments. This is deliberate — see the
derived-accelerator decision in ADR 0011.

## Divergence from ADR 0011's original tables

ADR 0011 (2026-08-16) published a different record layout: a schema-version byte at offset 1, flags
at 4, side/event-kind at 2/3, and a local receipt timestamp at 16. The implementation does not
match, and this file supersedes it. The differences and why they are acceptable:

- **Per-record schema version — dropped.** The header already carries major/minor version and record
  size, and a segment is written once by one writer, so records cannot disagree.
- **Per-record flags — dropped.** Reclaimable from reserved space if a per-record marker is needed.
- **Local receipt timestamp — dropped.** Provenance lives in raw captures under the
  derived-accelerator decision.
- **`amount_traded` — added at offset 40.** Required by ADR 0013's shortfall correction.
