#include <te/feed/trade_reconciler.hpp>

namespace te {

void TradeReconciler::observe(const OrderEvent& event) {
    switch (event.kind) {
        case EventKind::add:
        case EventKind::modify:
            // Quantity is the resulting resting amount, not a delta.
            resting_[event.order_id] = RestingInfo{event.side, event.price, event.quantity};
            break;
        case EventKind::remove:
            resting_.erase(event.order_id);
            break;
    }
}

std::vector<OrderEvent> TradeReconciler::reconcile(const TradeEvent& trade) {
    std::vector<OrderEvent> corrections;

    for (OrderId id : {trade.buy_order_id, trade.sell_order_id}) {
        const auto it = resting_.find(id);
        if (it == resting_.end()) {
            // Common for the taking order, which traded before ever becoming resting state.
            continue;
        }

        const RestingInfo info = it->second;
        // Both trade IDs are checked independently. A known resting side gets one correction;
        // a full fill removes it and a partial fill stores the resulting quantity.
        if (trade.quantity.units >= info.quantity.units) {
            corrections.push_back(OrderEvent{
                .venue_timestamp_us = trade.venue_timestamp_us,
                .order_id = id,
                .price = info.price,
                .quantity = info.quantity,
                .side = info.side,
                .kind = EventKind::remove,
            });
            resting_.erase(it);
        } else {
            const Qty remaining{info.quantity.units - trade.quantity.units};
            corrections.push_back(OrderEvent{
                .venue_timestamp_us = trade.venue_timestamp_us,
                .order_id = id,
                .price = info.price,
                .quantity = remaining,
                .side = info.side,
                .kind = EventKind::modify,
            });
            it->second.quantity = remaining;
        }
    }

    return corrections;
}

}  // namespace te
