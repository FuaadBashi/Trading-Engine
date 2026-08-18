# Slice 2 book structures: revision notes

**Snapshot:** 2026-08-17
**Purpose:** Same spirit as `slice-1-foundations-notes.md`, one slice early: how `OrderBook`'s
storage is shaped and why, written down before any of it is implemented. Read it beside ADR 0007
and ADR 0012, which it summarises and cross-references rather than replaces.

## 1. The three containers, and how they relate

```cpp
std::map<Price, PriceLevel>            bidLevels;
std::map<Price, PriceLevel>            askLevels;
std::unordered_map<OrderId, OrderLocator> locators;
```

Two structures, describing the same orders from two different angles:

- The **maps** answer "what's resting at this price" -- one per side, each key an occupied
  price, each value the state at that one price.
- The **id lookup** answers "where is this specific order" -- a `modify`/`remove` event only
  gives you an `OrderId`, so this is what turns that into a side, a price, and a position, without
  scanning anything.

Every `add` updates both. Every `remove` updates both. A `modify` might touch both too, depending
on whether the price changed (§3).

## 2. `map<Price, PriceLevel>` is one level per price, not a list of them

Easy to misread at first: the value type is `PriceLevel`, singular -- not a `vector<PriceLevel>`,
not a list of levels sharing a price. One price, one `PriceLevel` object. The *list* lives one
layer deeper, **inside** that single object:

```text
bidLevels[64840.11]  ->  one PriceLevel, holding:
                            total_quantity  (see §4)
                            a FIFO of the orders resting at exactly this price
```

Concretely, three orders resting at the same price look like this:

```text
key 64840.11  ->  PriceLevel {
                     total_quantity: 1.60 BTC
                     orders: [ id 101 (0.50) -> id 102 (0.75) -> id 205 (0.35) ]
                   }
```

Same reasoning for `unordered_map<OrderId, OrderLocator>`: one id, one locator, never a
collection -- an id can only belong to one order at a time (ADR 0012's duplicate-id rule makes
that a checked invariant, not just a convention). Each locator points *into* the exact same FIFO
slot shown above -- it is a second index into the same orders, not a copy of them.

`Price` works as a map key at all because it already carries `operator<=>` (`core/types.hpp`) --
that comparison is the one thing `std::map` needs to keep its keys ordered, and it was already
there from Slice 1 for an unrelated reason.

## 3. What each container still needs a real shape

Named in ADR 0007's decision, neither has one yet -- both are yours to design while coding, not
something decided here in advance.

**`PriceLevel`.** At minimum: a cached `total_quantity` (§4), and the FIFO itself. ADR 0007 is
explicit that this should be "an ownership-safe standard-library queue/list... before any custom
pool or intrusive structure is introduced" -- something like `std::list<OrderId>`, not the
intrusive list, for this reference implementation.

**`OrderLocator`.** Needs enough for `apply()` to reach an order directly: which side, which
price (so you know which map and which key), and something identifying the order's exact spot in
that price's FIFO. If the FIFO ends up a `std::list`, a `std::list<OrderId>::iterator` is worth
considering for that last part specifically -- iterators into a `std::list` stay valid across
other insertions and removals elsewhere in the same list, which a `std::vector` or `std::deque`
iterator would not promise.

## 4. The "running total" on `PriceLevel`

A cached number, kept current incrementally rather than recomputed from scratch:

```text
before: PriceLevel @ 64840.11 = { total: 1.25, orders: [101, 102] }
order 205 (0.35 BTC) joins
after:  PriceLevel @ 64840.11 = { total: 1.60, orders: [101, 102, 205] }
```

Add the new order's quantity when it joins; subtract when one leaves or shrinks. That is the
entire trick -- without it, `qtyAt()` would have to walk every order at a price and sum them on
every single call. With it, `qtyAt()` is one map lookup plus reading one field. Same idea as a
bank keeping a running balance instead of re-adding every transaction each time it is checked.

## 5. Resolving the Modify rule -- Cancel/Replace

ADR 0012 already states the resolved rule (see that ADR for the authoritative text and the
measured evidence: 72 same-ID price moves in the hour-long reference capture). Restated briefly
here because it is the one place the two containers in §1 genuinely interact:

- Reported price **equals** the order's stored price: in-place update. Adjust that one
  `PriceLevel`'s total by the quantity delta. Position in the FIFO is untouched.
- Reported price **differs** from the stored price: cancel/replace. Remove the order from its old
  `PriceLevel` (total down, the level may empty out entirely), insert it fresh into the new
  `PriceLevel` (total up, joins the back of that level's queue), and update its `OrderLocator` to
  point at the new location.

The named industry concept is worth knowing regardless of this specific book: FIX protocol's
`OrderCancelReplaceRequest` (tag 35=G) is exactly this -- change an order's price and/or quantity
under the same reference id. It is a **common convention** on price-time-priority venues that
changing price forfeits queue position while a same-price quantity *decrease* keeps it.

**That convention is not proven for Bitstamp**, and this document should not be read as claiming
it is. An earlier revision called it a universal rule, which overstated the evidence and
contradicted ADR 0008, which leaves Bitstamp's queue-priority semantics deliberately unresolved
pending joined order/trade data. What the reference book does on a price change -- move the order
to the new level, joining the back of that queue -- is therefore its **provisional deterministic
policy**, chosen because it is defensible and reproducible, not because Bitstamp's matching engine
has been observed to behave that way. ADR 0008 owns the real answer when the evidence exists.

`ApplyError` accordingly has no `price_mismatch` case -- a changed price is a handled path, not a
rejected one.

## 6. `bestBid()` / `bestAsk()` come from the map's own ordering

Both maps are sorted ascending by `Price`, always -- that is `std::map`'s nature, not something
maintained separately. "Best" means opposite ends for the two sides, because the two sides want
opposite things:

```text
bids, ascending:  64840.09   64840.10   64840.11 *          |  spread  |
asks, ascending:                                            | 64840.12* 64840.13   64841.00
```

- Bids want the *highest* price, and the highest value in an ascending sequence is always last --
  so `bestBid()` is the map's final element (`bidLevels.rbegin()`, or one step back from
  `end()`).
- Asks want the *lowest* price, and the lowest value in an ascending sequence is always first --
  so `bestAsk()` is the map's first element (`askLevels.begin()`).

Neither side needs its own "what's the current best price" bookkeeping. The sort order the map
already maintains *is* that answer. Per ADR 0012, both return `std::optional<Price>` -- an empty
side is absence, not a sentinel price.

## 7. The classification boundary, before the book

Not every decoded event belongs in the book, and deciding which do is venue knowledge that
`OrderBook` must not carry. `BitstampEventClassifier` (`feed/bitstamp_classifier.hpp`) sits
between the decoder and the book and answers exactly one question per event: apply it, or skip
it and count it.

```text
raw JSON -> decodeBitstampEvent -> OrderEvent -> BitstampEventClassifier -> OrderBook
                                                          |
                                                          +-> skipped, and counted
```

What it excludes today is one specific, measured case: **price-zero lifecycles**. A price of zero
is not a real book price, so an order created at zero would invent a level beneath the entire bid
ladder that nobody can trade against. The rule is stateful, because only the *create* is
recognisable -- the later change/delete for the same order carry ordinary-looking prices, so the
id has to be remembered until the lifecycle ends with a remove.

Three properties worth holding onto:

- **`apply_to_book` is not a claim of resting liquidity.** It means "not positively identified as
  something else." Bitstamp's `live_orders` also carries marketable order lifecycles that can
  cross the visible book and rest briefly or never; separating those needs joined order/trade
  evidence the classifier does not have.
- **Everything skipped is counted** (`ClassifierStats`), the same reasoning as the Slice 1
  recorder's counters: a run that quietly discarded a large share of its input is indistinguishable
  from a healthy one unless the discards are visible.
- **`OrderEvent` is unchanged by this.** Bitstamp's `order_subtype` stays undecoded on purpose --
  see ADR 0011's open question, which blocks that on joined order/trade data rather than letting
  the legacy 56-byte record's shape settle the permanent domain model.

Cross-checked against `scripts/audit_book_bootstrap.py` over the 252,374-event hour capture: both
implementations agree exactly -- 4 zero-price lifecycle events, 252,370 applied, 0 left open. The
29-second reference segment contains none at all, which is why the hour capture is the one that
actually exercises this.

## 8. Where this leaves the actual coding

Nothing in this file is code, and nothing here decides `OrderLocator`'s or `PriceLevel`'s member
names, types beyond what §3 already names, or `apply()`'s actual body -- that is the work ahead,
per this project's own rule that bodies are the project. What this file is for: so the shape of
the two containers, why a running total exists, and why a price-changing modify is not an error,
do not have to be re-derived while also writing the first working line of `PriceLevel`.
