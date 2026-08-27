#include <algorithm>
#include <te/feed/trade_reconciler.hpp>

namespace te {

void TradeReconciler::observe(const OrderEvent& event, Qty amountTraded) {
    switch (event.kind) {
        case EventKind::add:
            resting_[event.order_id] = RestingInfo{event.side, event.price, event.quantity};
            break;
        case EventKind::modify: {
            // Quantity is the resulting resting amount, not a delta.
            resting_[event.order_id] = RestingInfo{event.side, event.price, event.quantity};

            auto ledgerIt = fillLedger_.find(event.order_id);
            if (ledgerIt == fillLedger_.end()) {
                if (amountTraded.units > 0) {
                    fillLedger_.emplace(event.order_id,
                                        AmountTradeInfo{event.venue_timestamp_us, amountTraded});
                }
                break;
            }

            AmountTradeInfo& info = ledgerIt->second;
            if (info.timestamp == event.venue_timestamp_us) {
                info.quantity.units += amountTraded.units;
            } else {
                if (info.quantity.units > 0) {
                    ++stats_.staleFillsDiscarded;
                }
                if (amountTraded.units > 0) {
                    info = AmountTradeInfo{event.venue_timestamp_us, amountTraded};
                } else {
                    fillLedger_.erase(ledgerIt);
                }
            }
            break;
        }
        case EventKind::remove: {
            resting_.erase(event.order_id);

            const auto ledgerIt = fillLedger_.find(event.order_id);
            if (ledgerIt != fillLedger_.end()) {
                // A credit at this same timestamp never had a chance to clear: order events win
                // exact ties, so this delete ran before any trade at the same microsecond. Only an
                // older unmatched credit means a trade genuinely never arrived.
                if (ledgerIt->second.quantity.units > 0 &&
                    ledgerIt->second.timestamp != event.venue_timestamp_us) {
                    ++stats_.ordersRemovedWithUnmatchedFill;
                }
                fillLedger_.erase(ledgerIt);
            }
            break;
        }
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

        const RestingInfo restingInfo = it->second;
        std::int64_t shortfallUnits = trade.quantity.units;

        auto ledgerIt = fillLedger_.find(id);
        if (ledgerIt != fillLedger_.end()) {
            AmountTradeInfo& credit = ledgerIt->second;
            if (credit.timestamp == trade.venue_timestamp_us) {
                const std::int64_t coveredUnits = std::min(shortfallUnits, credit.quantity.units);
                shortfallUnits -= coveredUnits;
                credit.quantity.units -= coveredUnits;
                if (credit.quantity.units == 0) {
                    fillLedger_.erase(ledgerIt);
                }
            } else {
                if (credit.quantity.units > 0) {
                    ++stats_.staleFillsDiscarded;
                }
                fillLedger_.erase(ledgerIt);
            }
        }

        if (shortfallUnits == 0) {
            continue;
        }

        // Both trade IDs are checked independently. A known resting side gets one correction;
        // only the part not already reported by live_orders changes the shadow book.
        if (shortfallUnits >= restingInfo.quantity.units) {
            corrections.push_back(OrderEvent{
                .venue_timestamp_us = trade.venue_timestamp_us,
                .order_id = id,
                .price = restingInfo.price,
                .quantity = restingInfo.quantity,
                .side = restingInfo.side,
                .kind = EventKind::remove,
            });
            resting_.erase(it);
        } else {
            const Qty remaining{restingInfo.quantity.units - shortfallUnits};
            corrections.push_back(OrderEvent{
                .venue_timestamp_us = trade.venue_timestamp_us,
                .order_id = id,
                .price = restingInfo.price,
                .quantity = remaining,
                .side = restingInfo.side,
                .kind = EventKind::modify,
            });
            it->second.quantity = remaining;
        }
    }

    return corrections;
}

}  // namespace te
