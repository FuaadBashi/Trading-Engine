#pragma once

#include <unordered_map>
#include <vector>

#include <te/core/types.hpp>
#include <te/feed/events.hpp>
#include <te/feed/trade_event.hpp>

namespace te {

/**
 * @brief  Corrects for order removals live_orders alone does not reliably report.
 *
 * @note   Traced against a real capture: an order can leave the book via a fill with no
 *         order_deleted ever sent, or never rest at all if it's fully consumed immediately --
 *         either way it's invisible on live_orders and only shows up as a party to a trade.
 *         Keeps its own minimal shadow of resting orders (side, price, quantity, by id) built
 *         from the same events OrderBook sees, so it composes as one more pipeline stage --
 *         decode -> classify -> reconcile -> apply -- without needing a live reference to the
 *         book it corrects. OrderBook itself never changes.
 */
class TradeReconciler {
public:
    /** @brief Keeps the shadow state in sync -- feed it every event that reaches apply(). */
    void observe(const OrderEvent& event);

    /**
     * @brief  Given one trade, returns the corrective OrderEvents needed, if any.
     *
     * @note   A correction may turn out redundant if live_orders reports the same removal
     *         moments later -- that second apply() call is expected to fail with
     *         unknown_order_id; treat it as a harmless no-op at the call site.
     */
    std::vector<OrderEvent> reconcile(const TradeEvent& trade);

private:
    struct RestingInfo {
        Side side;
        Price price;
        Qty quantity;
    };

    std::unordered_map<OrderId, RestingInfo, OrderIdHash> resting_;
};

}  // namespace te
