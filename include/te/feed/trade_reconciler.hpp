#pragma once

#include <te/core/types.hpp>
#include <te/feed/events.hpp>
#include <te/feed/trade_event.hpp>
#include <unordered_map>
#include <vector>

namespace te {

// Shadow of successfully applied resting orders. Trades can imply a missing modify/remove, so
// reconcile returns corrections without owning or mutating the real OrderBook.
class TradeReconciler {
public:
    // Call only after the same event successfully reaches OrderBook.
    void observe(const OrderEvent& event);

    // Unknown IDs are normal taker/already-removed orders and produce no correction. Never
    // observe generated corrections: reconcile already updates its own shadow.
    std::vector<OrderEvent> reconcile(const TradeEvent& trade);

    // Current limitation: the full trade is subtracted even when live_orders already reported
    // the same fill. ADR 0013's amount_traded shortfall design must land before real joined replay.

private:
    struct RestingInfo {
        Side side;
        Price price;
        Qty quantity;
    };

    std::unordered_map<OrderId, RestingInfo, OrderIdHash> resting_;
};

}  // namespace te
