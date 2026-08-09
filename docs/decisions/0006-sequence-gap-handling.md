# ADR 0006: Behaviour on a lost-event gap

- **Status:** accepted
- **Date:** 2026-08-09
- **Slice:** 1

## Context

**Rewritten 2026-08-08.** The original framing assumed the venue numbers messages with a
monotonic integer sequence, as Coinbase does (`"sequence":133935289272`). The venue is now
Bitstamp (see [ADR 0010](0010-venue-selection.md)), which does not. It chains them:

```
"event_id":     "0006588e-fbcf-19b0-0000-000100000020"
"pre_event_id": "0006588e-fbcf-5278-0000-000000000020"
```

Each event names its predecessor. This changes the detection algorithm, not the problem. The
problem is unchanged and is the reason this ADR exists: a gap means book state has been lost, and
silently continuing produces a wrong book that looks entirely healthy. Every downstream number —
queue position, fill prediction, PnL — inherits the error with no visible symptom.

The differences that matter for implementation:

- **Detection is a link check, not an arithmetic one.** You cannot test `expected == actual + 1`.
  You hold the previous event's `event_id` and test that the next event's `pre_event_id` matches
  it. Cheap, but it means carrying state across events rather than a single integer counter.
- **A gap gives you no magnitude.** An integer sequence tells you *how many* messages were lost.
  A broken chain tells you only *that* the chain broke. Any policy that depends on gap size
  ("re-snapshot if more than N missing") is unavailable.
- **The venue can ask you to reconnect.** Bitstamp emits `bts:request_reconnect`, which is a
  planned discontinuity rather than a fault, and should not be recorded as a data-quality gap.
- **There is a second discontinuity at startup.** The book is seeded from a REST snapshot while
  the socket is already subscribed, so the beginning of every capture contains events that
  predate the snapshot and must be discarded by `microtimestamp`. That boundary is a gap-shaped
  thing that is not a gap, and the replay path has to know the difference.

## Options considered

1. **Reconnect and re-snapshot** — correct, loses a window of data, and complicates the capture
   format because the file now contains multiple snapshot boundaries rather than one at the head.
2. **Mark a gap record in the capture and continue** — keeps the stream intact and honest, pushes
   the burden onto every downstream consumer to handle discontinuity rather than assume continuity.
3. **Both: mark the gap AND re-snapshot** — most work, most correct, and the only option that
   lets a later analysis quantify how much data was lost and when.
4. **Detect but do not record** — log to stderr and carry on. Cheapest, and quietly destroys the
   corpus's value as evidence, because a replay months later cannot tell a clean run from a lossy
   one. Listed to be rejected explicitly.

## Decision

For every consecutive Bitstamp order event within one capture segment, require:

```
current.pre_event_id == previous.event_id
```

Treat a mismatch as loss of book continuity, regardless of how plausible the resulting book
looks. Mark the in-memory book invalid immediately, stop applying order events, record a
diagnostic containing the previous `event_id`, the unexpected `pre_event_id`, timestamps and
capture segment, then reconnect and obtain a fresh `group=2` snapshot before resuming.

Keep raw JSONL files payload-only. Synthetic gap and snapshot markers do not belong in a file
described as captured venue payloads. A capture manifest records segment boundaries, snapshots,
requested reconnects and detected gaps. The versioned binary format contains explicit boundary
and gap record types so replay cannot cross a discontinuity accidentally.

A `bts:request_reconnect` closes the current segment as a planned boundary and starts a new
snapshot-backed segment; it is not labelled packet loss. At startup, replay uses the snapshot's
`microtimestamp` as the boundary, ignores earlier buffered events, and then requires a valid
event-ID chain among all applied events. No book is considered valid until both conditions hold.

## Consequences

- A corrupted or incomplete stream fails closed instead of producing a silently incorrect book.
- Capture output becomes a set of snapshot-backed segments plus a manifest, rather than one
  infinitely appendable file with ambiguous continuity.
- Raw venue payloads remain suitable for decoder golden tests because capture metadata is kept
  separately.
- The binary record schema must distinguish data events, snapshot boundaries, requested
  reconnects and detected gaps. Golden tests must cover each record kind.
- Recovery loses the interval between the gap and the new snapshot; the manifest makes that
  missing interval explicit to later analysis.
- UUID-shaped chain identifiers require exact comparison. They may be parsed to fixed 16-byte
  values after validating their text form, but must never be compared numerically.
- The snapshot timestamp is a venue-provided alignment boundary, not proof that no upstream
  event was lost. Coinbase L2 top-of-book comparison provides an additional validation signal,
  not a replacement for the Bitstamp chain check.

## How you would defend this in an interview

The original design expected consecutive integers, but the selected venue exposes a predecessor
chain. I therefore validate links rather than arithmetic increments. A broken link proves loss
of continuity but not its magnitude, so the safe response is to invalidate the book, preserve
evidence of the gap and restart from a fresh snapshot. Planned reconnects and startup alignment
are recorded as different boundary types so replay cannot confuse them with data loss.

---

## Amendment 1 — magnitude is recoverable modulo 4 (2026-08-09)

The decision above stands. One factual claim in it is too strong: that a broken link proves loss
of continuity "but not its magnitude". Inspecting the `event_id` structure across the first 60,000
events of the hour capture shows the last UUID group is not opaque. It is a dense four-state
counter:

```
...-000100000020  14,999
...-000101000020  15,000
...-000102000020  15,000
...-000103000020  15,000
```

Deltas are exactly `+0x01000000` three times then `-0x03000000` on wrap — no other value occurs.
So the number of missing events is recoverable **modulo 4**. At the single break in the hour
capture the counter advanced one slot, meaning 1, 5, 9, ... events were lost.

This does not change the policy. A gap of unknown-but-congruent size still invalidates the book,
and mod-4 cannot distinguish a one-event drop from a burst of 4n+1. It changes only what the
manifest can honestly record: `lost ≡ k (mod 4)` rather than `lost = unknown`. Anything stronger
would require assuming the counter is a global sequence rather than a per-connection artefact,
which has not been established.

## Amendment 2 — the reader must be draining before the snapshot is taken (2026-08-09)

The decision says the socket is subscribed before the snapshot is acquired, so that buffered
events cover the snapshot instant and replay discards those predating it. The first hour-long
capture satisfied that on paper and violated it in practice: the capture script awaited the
snapshot HTTP fetch before entering its receive loop, so nothing drained the socket during the
round trip.

Measured consequence: the snapshot is timestamped **0.608 s before the first recorded event**,
the reverse of the intended relation. Replaying snapshot + stream leaves **11 `order_deleted`
events referencing orders never seen** — orders created inside the hole and deleted after it.
All 11 fall in the first 1,592 lines; the book is self-consistent for the remaining 59 minutes.

The invariant is therefore stronger than "subscribe first". It is: **a task must already be
writing frames to disk before the snapshot request is issued.** Subscribing is not sufficient,
because a subscribed socket that nothing is reading still loses the window.

Replay must be able to discard events that predate the snapshot; it cannot invent ones missing
after it. Where the two cannot be ordered correctly, the correct reading is that the segment has
a warm-up region whose length is the snapshot-to-first-event interval, and the book is not valid
within it.

## Measured evidence (hour capture, 2026-08-09)

| Property | Value |
|---|---|
| Events | 252,374 |
| Duration | 3,599.3 s |
| Rate | 70.1 events/s |
| Chain breaks | 1 (line 162,871) |
| Loss rate | 1 in 252,374 (0.0004%) |
| Orphaned deletes (warm-up) | 11, all within first 1,592 lines |
| Duplicate creates / orphaned changes | 0 / 0 |

Validated by `scripts/validate_capture.py`, the Python reference implementation of the chain rule
above. The C++ decoder is checked against it rather than against its own assumptions.
