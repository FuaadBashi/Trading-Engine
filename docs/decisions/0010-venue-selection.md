# ADR 0010: Primary venue for L3 capture

- **Status:** accepted
- **Date:** 2026-08-09
- **Slice:** 1

## Context

The project plan (§3) locked in Coinbase Exchange's `full` channel on the grounds that it was
the only venue offering free, public, order-by-order (L3) data at this data budget. L3 is not a
preference: §5 targets fill realism L3 (queue-position modelling) and L4 (latency distribution),
and queue position cannot be modelled from price-aggregated L2 depth. The venue choice therefore
determines whether the project's stated deliverable is reachable at all.

That premise turned out to be wrong, and was caught by the 30-second probe that step 2 of the
Slice 1 action plan exists to force:

```
{"type":"error","message":"Failed to subscribe",
 "reason":"level2, level3, and full channels now require authentication."}
```

Authentication alone would not have been disqualifying. The constraint is *which* Coinbase
product the endpoint belongs to. `ws-feed.exchange.coinbase.com` is Coinbase **Exchange**, whose
authenticated APIs require an Exchange account; the project's current retail setup provides
Advanced Trade access instead. Advanced Trade exposes public L2 market data but not the Exchange
`full` order lifecycle required here. Therefore Coinbase L3 is unavailable under the project's
current account and cost constraints. This ADR does not claim that institutional Exchange access
is technically impossible; gaining suitable Exchange access is an explicit revisit condition.

A subsequent unauthenticated probe against Bitstamp's `live_orders` channel returned per-order
records immediately, with no account of any kind:

```
["65015.53","0.15253549","2037291445604352"]
["65015.53","0.19000000","2037291445903361"]
["65015.53","0.25000000","2037291611193345"]
```

Three distinct orders resting at one price, in queue order. That is the input a queue model needs.

## Options considered

1. **Coinbase Advanced Trade, drop to L2-only realism** — no new venue code, but caps the fill
   ladder at L2, which removes the queue-position work and weakens the project's headline claim
   to something a spreadsheet could produce. Rejected: it discards the reason the project exists.
2. **Bitstamp `live_orders`** — the live probe and `group=2` snapshot expose individual order IDs
   at shared price levels plus create/change/delete events, publicly and without authentication.
   Costs a REST snapshot-seeding step because the socket sends no initial book, and venue-specific
   event-chain handling.
3. **Bitfinex raw books (`R0`)** — also order-by-order and public. Not evaluated in depth; held in
   reserve as a second L3 venue if one is wanted later.
4. **Paid historical L3 (Tardis.dev, Databento, CoinAPI)** — real L3 including equities and
   futures, but recurring cost in the $200–600/month range for a project whose deliverable is a
   validated simulator rather than a traded strategy. Rejected on cost/benefit, not capability.

## Decision

Use Bitstamp `live_orders_btcusd` as the primary L3 event source and seed each continuous capture
segment with Bitstamp's `group=2` BTC/USD REST snapshot. Use Coinbase `level2_batch` as a secondary,
independent L2 source for best-bid/best-ask and aggregated-level sanity checks. Keep both venue
schemas outside the core order-book model behind venue-specific decoders and the `Feed` boundary.

## Consequences

- Bitstamp makes queue-position research possible using individual resting-order IDs and
  lifecycle events under the current zero-data-budget constraint.
- Every Bitstamp capture segment requires socket subscription before snapshot acquisition,
  buffered-event alignment and event-chain validation as specified by ADR 0006.
- Coinbase cannot validate individual Bitstamp orders or exact quantities because it is a
  different venue. It can only provide an independent directional/top-of-book sanity check;
  temporary price divergence is not automatically a defect.
- `Feed` is a real normalization boundary: Bitstamp uses predecessor-linked L3 events while
  Coinbase uses batched L2 updates with different schemas and semantics.
- Decoder fixtures, record metadata and measurements must identify venue, instrument and schema
  version. Data from the two venues must never be merged as though it were one order book.
- Bitstamp-specific IDs and fields must not leak into the core book API.
- Revisit this decision if suitable Coinbase Exchange access becomes available, Bitstamp changes
  or withdraws the public channel, capture validation shows unrecoverable ambiguity, or the
  project adds an equities venue.

## How you would defend this in an interview

I did not select a venue from documentation alone. A probe disproved the original Coinbase
assumption, so I returned to the actual requirement—individual order events for queue modelling—
and verified Bitstamp with both a `group=2` snapshot and live create/change/delete events. I kept
Coinbase L2 as an independent top-of-book check and isolated both protocols behind feed adapters,
while documenting the weaker point that cross-venue prices are only a sanity signal, not an exact
reconciliation.
