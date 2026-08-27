#pragma once

#include <cstddef>
#include <cstdint>
#include <te/core/types.hpp>
#include <te/feed/events.hpp>
#include <te/feed/trade_event.hpp>
#include <unordered_map>
#include <vector>

namespace te {

// Health counters for fills one stream reported and the other never confirmed. Both stay at zero
// on a capture where every reported fill is matched by a trade.
struct ReconcilerStats {
    // An order left the book still holding a credit from an EARLIER timestamp, so no trade ever
    // matched that fill. Excludes the case where the fill-carrying change and the delete share a
    // timestamp: order events win exact ties, so the delete always lands before that timestamp's
    // trades and the credit cannot clear. That is structural, not a fault.
    std::size_t ordersRemovedWithUnmatchedFill{};

    // A credit was dropped because a newer timestamp arrived for the same order. Inputs are
    // time-ordered, so the older timestamp's trade can no longer be coming.
    std::size_t staleFillsDiscarded{};
};

// Shadow of successfully applied resting orders. Trades can imply a missing modify/remove, so
// reconcile returns corrections without owning or mutating the real OrderBook.
class TradeReconciler {
public:
    // Call only after the same event successfully reaches OrderBook.
    void observe(const OrderEvent& event, Qty amountTraded);

    // Unknown IDs are normal taker/already-removed orders and produce no correction. Never
    // observe generated corrections: reconcile already updates its own shadow.
    std::vector<OrderEvent> reconcile(const TradeEvent& trade);

    const ReconcilerStats& stats() const { return stats_; }

private:
    struct RestingInfo {
        Side side;
        Price price;
        Qty quantity;
    };

    struct AmountTradeInfo {
        std::uint64_t timestamp{};
        Qty quantity{};
    };

    ReconcilerStats stats_{};

    std::unordered_map<OrderId, RestingInfo, OrderIdHash> resting_;
    std::unordered_map<OrderId, AmountTradeInfo, OrderIdHash> fillLedger_;
};

}  // namespace te
