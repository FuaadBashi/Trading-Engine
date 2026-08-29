# Decisions

One file per real decision, written the day it is made. Numbered, never deleted, superseded
rather than edited when it changes.

This folder exists for two reasons. It stops you re-litigating settled questions at 1am, and
it gives you a ready answer when an interviewer asks why L3 over L2, or why int64 over double,
or how you handled the cancel-ambiguity problem. Ten minutes per entry.

Start from `0000-template.md`. The stubs below are pre-filled with the context and the options
so that all you have to write is the decision and the defence. That is deliberate: the options
are research, the decision is yours.

| ADR | Title | Slice | Decide by |
|---|---|---|---|
| 0001 | Dependency management | 0 | before first build |
| 0002 | Exceptions on the hot path | 0 | before first build |
| 0003 | Error representation in the hot path | 0 | before first interface |
| 0004 | int64 ticks for price and PnL | 1 | before `types.hpp` |
| 0005 | Order ID representation | 1 | before `events.hpp` |
| 0006 | Behaviour on a lost-event gap | 1 | before the recorder runs unattended |
| 0007 | Price level storage in the order book | 2 | before `order_book.hpp` |
| 0008 | Queue-position assumption for cancels | 4 | before `queue_model.hpp` |
| 0009 | Virtual dispatch vs CRTP for Strategy | 3 | before `strategy.hpp` |
| 0010 | Primary venue for L3 capture | 1 | before the hour-long capture |
| 0011 | Portable binary segments and snapshots | 1-2 | before durable binary corpus |
| 0012 | Reference order-book API and failure contract | 2 | before `order_book.cpp` |
| 0013 | Merge ordering and fill double-counting | 2 | accepted 2026-08-27 |

Later additions with no stub yet: live-leg realism upgrade (week 11), equities adapter timing
(week 12 to 14), strategy choice for the fill ladder (week 8).

## Amended 2026-08-09 after the first live probes

0004, 0005 and 0006 were originally written against Coinbase and were revised after ADR 0010
selected Bitstamp. The accepted decisions now reflect evidence from the captured `group=2`
snapshot and `live_orders` stream: exact decimal-string parsing, 64-bit Bitstamp order IDs and
predecessor-chain gap detection.
