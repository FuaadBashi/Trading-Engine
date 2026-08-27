#pragma once

#include <list>
#include <optional>

#include "te/book/order_book.hpp"
#include "te/feed/bitstamp/snapshot.hpp"

namespace te {

class TradeReconciler;

namespace bitstamp {

// Builds a fresh book from snapshot state. An optional reconciler observes each successful add,
// otherwise snapshot-only orders could never be corrected by later trades.
Result<OrderBook, ApplyError> bootstrap(BookSnapshot snapshot,
                                        TradeReconciler* reconciler = nullptr);

}  // namespace bitstamp
}  // namespace te
