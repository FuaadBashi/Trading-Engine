# ADR 0015: Allocation failure is fatal to the process

- **Status:** accepted — explicitly an interim position, see "What would make you revisit it"
- **Date:** 2026-09-16
- **Slice:** 2

## Context

`OrderBook::applyAdd` creates a price level and then inserts an order into it. Both steps allocate:
`std::map::try_emplace` for the level node, `std::list::insert` inside `PriceLevel::addOrder` for the
order node, and `std::unordered_map::emplace` for the index entry. Any of them can throw
`std::bad_alloc`.

Two separate passes had already added `catch (const std::bad_alloc&)` blocks returning a new
`ApplyError::allocation_failure` — first around `orderIndex_.emplace` (`22bc074`), later around the
`addOrder()` calls in both `applyAdd` and `applyModify` (`c43acc0`). Neither pass recorded a
decision. The code had chosen "recoverable" twice by accident.

Two facts forced the question rather than letting the catches stand:

**An orphaned empty level is worse than it looks.** `bestBid()`/`bestAsk()` read the level map, not
quantities, so an empty level *is* the best price. `qtyAt()` then reports zero behind it. And
`digest()` skips zero-quantity levels, so the fingerprint is unchanged. A book that leaked an empty
level lies about top-of-book while hashing identically to a correct one — invisible to the one check
that exists to catch corruption.

**`ApplyError` is scoped to bad market input.** ADR 0012 states that internal invariant failure is
"a programming/system failure, not bad market input". Heap exhaustion is a system failure. Putting
`allocation_failure` into `ApplyError` widened that contract without saying so, and invited callers
to treat process death as a skippable bad event.

## Options considered

The question is really two independent ones — does the process survive, and is the object still
valid if someone catches — which gives three viable pairs:

1. **Fatal process, unsafe object** — let it throw, do not roll back. No additional rollback
   machinery on the success path. Requires that no caller retain and reuse the failed book;
   process termination is a separate policy that must be enforced by the calling boundary.
2. **Fatal process, safe object** — an RAII rollback guard restores the book, then the exception
   continues to propagate. The book is never observable in a corrupt state regardless of who
   catches. This is the C++ strong exception guarantee, the same promise `std::map::insert` makes.
3. **Recoverable `ApplyError`** — RAII rollback, then return `allocation_failure`. Matches
   `apply()`'s signature, but owes a test that genuinely injects `bad_alloc` and owes every caller a
   policy. `replay.cpp` already treats nearly every `apply()` failure as a hard replay failure, so
   "recoverable at the book" is fatal at the only real caller anyway.

## Decision

**Option 1, as an interim position.** Allocation failures are not caught, not rolled back, and not
represented in `ApplyError`. `ApplyError::allocation_failure` and all three `catch` blocks are
removed. A `std::bad_alloc` propagates out of `apply()`; the intended caller policy is process death.

The original review found no project-owned catch blocks above the book. That is not a local
termination guarantee: dependency/test harnesses or future bindings may catch exceptions too.
The load-bearing condition is that no boundary can catch and then expose/reuse the failed book.

**Correction, 2026-09-18:** the accepted interim choice is unchanged. Before adding a recovery
wrapper or binding, explicitly enforce the fatal boundary or move to safe-object rollback.
Documentation of intended process death is not evidence that `apply()` enforces it.

## Consequences

- No extra rollback code, and `ApplyError` stays honestly
  scoped to bad market input as ADR 0012 requires.
- The correctness argument is a whole-program property, not a local one. The first `catch (...)` or
  `catch (const std::exception&)` added anywhere upstream silently converts this from "fine" to
  "ghost book". That is a real fragility, accepted knowingly rather than overlooked.
- A failed destination allocation during a price change can leave an empty level. The earlier
  duplicate-order warning concerned a hypothetical throw after successful destination insertion;
  the current intervening value construction and removal path has no identified allocation point.
  Do not describe that hypothetical window as a reproduced current allocation failure.
- Fault injection and a subprocess/death test can meaningfully verify an enforced fatal policy.
  The existing quantity-overflow tests cover the graceful `nullopt` rollback path, a **normal
  return with different control flow**; they do not establish allocation-failure behavior.

### Tension with ADR 0002, stated plainly

ADR 0002 says exceptions "must not escape latency-sensitive hot-path operations such as applying
market events, updating the order book". Taken literally, this decision violates that. The
reconciliation: ADR 0002's target is exceptions used for *recoverable, expected* failures and the
latency unpredictability of throwing during normal operation. `bad_alloc` is neither expected nor
recoverable — nothing resumes matching after the heap is gone, and the latency of the final throw
before process death is irrelevant. This ADR narrows ADR 0002 rather than contradicting it, but the
wording of ADR 0002 should be read with this exception in mind.

ADR 0002 also points at the stronger form of this decision: marking `apply()` `noexcept` so an
escaping `bad_alloc` calls `std::terminate` when it would escape the noexcept boundary. That would
*enforce* the precondition above instead of relying on a codebase-wide audit staying true. It is not
adopted here only because it was not the option chosen; it is the cheapest available hardening.

## What would make you revisit it

Any of these requires revisiting the integration boundary; option 2 is the planned safe-object
direction, while an enforced fatal boundary is an explicit alternative:

- A `catch (...)`, `catch (const std::exception&)`, or `catch (const std::bad_alloc&)` appearing
  anywhere that could sit above `apply()` on the stack — including a test harness, a logging
  wrapper, or the engine loop.
- `OrderBook` outliving the stack that threw, e.g. an engine that reseeds and continues.
- Any decision to treat the replay/engine loop as recoverable across an allocation failure.

This is a project policy, not a claim that every production matching engine makes the same choice.
Safe-object rollback is the planned direction; an enforced fatal boundary is another explicit
hardening option if the current process policy is retained. Select and test the approach before
introducing catching integration code.

## How you would defend this in an interview

Heap exhaustion is not a market-data error, so it does not belong in the enum describing bad market
data — putting it there invites a caller to skip the event and keep trading on a book that may be
lying about its best price. The intended process policy is fatal, but apply() does not enforce that
locally. Before recovery or bindings, I must enforce termination or implement tested RAII rollback
so a catching caller cannot reuse an inconsistent book. Process survival and object safety are
separate decisions.
