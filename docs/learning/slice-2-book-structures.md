# Slice 2 book structures: revision notes

**Snapshot:** 2026-08-20
**Purpose:** Same spirit as `slice-1-foundations-notes.md`: how `OrderBook`'s storage is shaped
and why. §1-8 were written before any of it was implemented; §9-10 were added once `apply()` and
the golden replay test existed to describe. Read it beside ADR 0007 and ADR 0012, which it
summarises and cross-references rather than replaces.

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

*(§9 picks up from here: by the time it was written, that work was done.)*

## 9. Copy and move: why `OrderBook` is move-only

`OrderLocator` (§3) turned out to hold an iterator, not a copy of anything -- `order_pos` is a
`std::list<RestingOrder>::iterator` sitting inside the exact same list a `PriceLevel` already
owns. That single fact is the whole reason `OrderBook` cannot use the copy constructor and
copy-assignment operator the compiler would otherwise generate for free.

**What a default copy would do.** Copying `bids_`/`asks_`/`orderIndex_` member-by-member copies
the maps and lists honestly -- new nodes, equal values. But copying an iterator only copies
*where it points*; it has no way to retarget itself at the new list. The result:

```text
original.orderIndex_[42].order_pos  ->  original's RestingOrder node

copy{original} constructed:
  copy.bids_[100]                 ->  a new PriceLevel, a new list, a new RestingOrder node
  copy.orderIndex_[42].order_pos  ->  still the same iterator value -- original's node, not copy's
```

`copy.qtyAt(...)` would look correct, because it only reads `copy.bids_`. But Modify or Remove on
order 42 goes through `copy.orderIndex_`, follows `order_pos`, and touches `original`'s order
instead -- and if `original` has since been destroyed, that iterator is dangling. Dereferencing it
is undefined behaviour: it may crash, corrupt memory, silently edit the wrong book, or simply
appear to work during testing and fail later. Same bug class as the dangling `Result::valueIf()`
sites found earlier this slice (`bitstamp_snapshot.cpp`) -- a reference that outlives what it
refers to, invisible until something actually exercises the stale copy.

**The fix, in `order_book.hpp`:**

```cpp
OrderBook() = default;

OrderBook(const OrderBook&)            = delete;
OrderBook& operator=(const OrderBook&) = delete;
OrderBook(OrderBook&&)                 = default;
OrderBook& operator=(OrderBook&&)      = default;
```

`= delete` turns the runtime bug above into a compile error instead: `OrderBook second{first};`
and `second = first;` both fail to build, for every caller, before the program ever runs. That is
not "safer" than the runtime failure it replaces -- it is a different category of failure, caught
at build time instead of possibly not at all.

**Why moving is still allowed.** Moving asks a different question than copying: not "build an
equivalent independent object," but "transfer ownership of the object that already exists." With
the standard allocators used here, moving a `std::map`, `std::list`, or `std::unordered_map`
transfers their existing nodes rather than duplicating them -- no node changes address, so every
`order_pos` iterator is still correct after the move:

```text
te::OrderBook destination{std::move(original)};

before:  original owns the nodes; every order_pos resolves inside original
after:   destination owns the identical nodes (not copies); every order_pos still resolves
         correctly, because the node itself never moved in memory -- only which OrderBook
         object owns it changed
```

`std::move` does not itself move anything -- it is a cast that makes the move constructor
eligible for overload resolution. The actual node transfer happens inside `std::map`'s and
`std::list`'s own move constructors, which `OrderBook`'s `= default` simply delegates to, member
by member. `original` remains destructible and assignable afterward, but nothing should be
assumed about what, if anything, still resolves inside it. This will matter directly for snapshot
seeding (task #14): the natural way to hand a freshly-built book to a replay controller is a
move, not a copy.

**The gotcha that makes the explicit list necessary.** Declaring a copy constructor -- even a
deleted one -- stops the compiler from implicitly generating the move constructor and
move-assignment operator. Writing only `OrderBook(const OrderBook&) = delete;` and stopping there
would have left `OrderBook` neither copyable nor movable, silently, with no obvious reason why
snapshot seeding's eventual move would refuse to compile. This is the classic **Rule of Five**:
once any one of the five special member functions is declared, state intent for all five, not
just the one that motivated the change.

| Operation | Declared as | Why |
| --- | --- | --- |
| `~OrderBook()` | not declared | standard containers clean up their own nodes; nothing custom to release |
| `OrderBook(const OrderBook&)` | `= delete` | a default copy would leave `order_pos` pointing into the wrong book |
| `operator=(const OrderBook&)` | `= delete` | same hazard, on assignment into an already-live book |
| `OrderBook(OrderBook&&)` | `= default` | standard containers transfer nodes on move; every locator stays correct |
| `operator=(OrderBook&&)` | `= default` | same guarantee, on assignment |

Also explicit for the same reason: `OrderBook() = default;`. It changes nothing by itself -- an
empty book from value-initialised maps is what would happen anyway -- but once any special member
is declared, a reader can no longer assume the rest are the ordinary compiler-generated ones, so
this states the full policy in one place rather than leaving part of it implicit.

**Compile-time proof, not just prose:**

```cpp
static_assert(!std::is_copy_constructible_v<OrderBook>, "...");
static_assert(!std::is_copy_assignable_v<OrderBook>,    "...");
static_assert( std::is_move_constructible_v<OrderBook>, "...");
static_assert( std::is_move_assignable_v<OrderBook>,    "...");
```

These belong beside the class because they are a permanent property of the *type*, not a fact
about any particular call site. If a future edit deletes the deleted-copy lines thinking them dead
code, the assertion fails at the point of the mistake, with a message that explains why -- rather
than as a dangling-iterator bug reported from wherever Modify or Remove eventually happened to
run. Verified against the header as it currently stands (`order_book.hpp:40-79`):
`clang++ -std=c++20 -Wall -Wextra -fsyntax-only` compiles clean, so all four assertions hold now,
not just in intent.

**The same pattern, twice already, elsewhere in this codebase:**

| Type | Owns | Copy | Move | Why |
| --- | --- | --- | --- | --- |
| `OrderBook` | interconnected containers -- locators hold iterators into its own lists | deleted | defaulted | a copy's iterators would still resolve into the original book |
| `Sink` (`telemetry/sink.hpp:115`) | an open output file stream | deleted | defaulted | two `Sink`s must not both believe they own the same stream |
| `TempFile` (`tests/unit/test_recorder.cpp:24`) | a path on disk, removed by its own destructor | deleted | not declared | two copies would each try to delete the same file; the second delete is a bug waiting to happen |

`TempFile` stops one step short of the other two only because nothing in its current use ever
needs to hand ownership onward -- there was no move to write, not a reason it couldn't have one.

The general question behind all three: *if every member were duplicated, would the new object be
completely independent and internally correct?* For plain values (`Price`, `Qty`, `OrderId`,
`OrderEvent`) the answer is trivially yes -- duplicate every field and the two objects owe each
other nothing. For anything that owns a resource or encodes a relationship between its own
members, the answer is usually no, and copying should be forbidden until something deliberately
writes a real deep copy -- for `OrderBook`, one that rebuilds every locator against the freshly
copied lists from scratch, which does not exist today.

## 10. `TradeReconciler`: what `live_orders` alone doesn't tell you

The golden replay test (`tests/unit/test_golden_replay.cpp`) found a real gap against Bitstamp's
own snapshot: 3 of 8,320 replayed orders were still resting in the book with no matching order in
the venue's later snapshot. All three sat in the starting snapshot, untouched by any event across
the entire capture -- no `order_deleted`, ever. A second, joined order+trade capture confirmed the
same pattern from a different angle: 8 of 468 trades referenced order ids that never appeared on
`live_orders` at all, only as a party to a trade.

Root cause: `live_orders` reliably announces cancels, but not every removal is a cancel. An order
fully consumed by a trade can leave the book with no `order_deleted`, and a marketable order that
crosses and fills instantly may never rest long enough to be announced at all. Neither is a code
bug -- `apply()` was told everything `live_orders` sent, correctly. The information gap is in the
venue feed, not the processing.

**The fix:** `TradeReconciler` (`feed/trade_reconciler.hpp`), fed by a new `live_trades` decoder
(`decodeBitstampTrade`, `feed/bitstamp_trade_decoder.hpp`). It keeps a minimal shadow of resting
orders (side, price, quantity, by id), built from the same events `apply()` sees. When a trade
references an id it still believes is resting, it emits a `modify` (partial fill) or `remove`
(full fill) `OrderEvent`, fed through the same `apply()` as everything else -- `OrderBook` never
needs to know trades exist. A correction that turns out redundant (`live_orders` reports the same
removal moments later) fails `apply()`'s existing `unknown_order_id` check harmlessly; nothing
new was needed there.

Verified against the real joined capture: 438 corrections emitted, 22 confirmed redundant by
actually failing that way, `validate()` clean throughout. Not yet wired into a live/replay driver
-- this is the reconciler itself, proven correct in isolation. See task "Investigate order_subtype
with joined order/trade data" for the open thread this closes the first real evidence for.

All five lifecycles (both feeds sufficient, both feeds required, and the case where nothing was
ever wrong), diagrammed: `output/pdf/Trading_Engine_TradeReconciler_Lifecycles.pdf`.
