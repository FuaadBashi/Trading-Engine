#pragma once

#include <cstddef>
#include <cstdint>
#include <te/core/types.hpp>
#include <te/feed/events.hpp>
#include <te/feed/trade_event.hpp>
#include <unordered_map>
#include <vector>

namespace te {

struct ReconcilerStats {
    std::size_t ordersRemovedWithOpenFillBalance{};
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
