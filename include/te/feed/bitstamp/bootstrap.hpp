#pragma once

#include <list>
#include <optional>

#include "te/book/order_book.hpp"
#include "te/feed/bitstamp/snapshot.hpp"

namespace te {

class TradeReconciler;

namespace bitstamp {

// reconciler, if non-null, observes every seeded order so a pre-existing resting order
// (present only in the snapshot, never announced by live_orders) is still known to it.
Result<OrderBook, ApplyError> bootstrap(BookSnapshot parseText, TradeReconciler* reconciler = nullptr);

}  // namespace bitstamp
}  // namespace te