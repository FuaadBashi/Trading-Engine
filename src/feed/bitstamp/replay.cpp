#include <te/feed/bitstamp/replay.hpp>

#include <unordered_map>
#include <utility>

#include <te/feed/bitstamp/bootstrap.hpp>
#include <te/feed/bitstamp/classifier.hpp>
#include <te/feed/trade_reconciler.hpp>

namespace te::bitstamp {
namespace {

template <typename Event>
bool isTimeOrdered(const std::vector<Event>& events) {
    for (std::size_t index = 1; index < events.size(); ++index) {
        if (events.at(index).venue_timestamp_us < events.at(index - 1).venue_timestamp_us) {
            return false;
        }
    }
    return true;
}

bool processOrder(OrderBook& orderBook, TradeReconciler& tradeReconciler,
                  EventClassifier& classifier, ReplayStats& replayStats,
                  std::unordered_map<OrderId, Side, OrderIdHash>& correctionRemovals,
                  const OrderEvent& order) {
    ++replayStats.orderEventsRead;
    if (classifier.classify(order) != EventDisposition::apply_to_book) {
        return true;
    }

    const auto result = orderBook.apply(order);
    if (!result.hasValue()) {
        const auto correction = correctionRemovals.find(order.order_id);
        if (order.kind == EventKind::remove && result.errorIf() != nullptr &&
            *result.errorIf() == ApplyError::unknown_order_id &&
            correction != correctionRemovals.end() && correction->second == order.side) {
            correctionRemovals.erase(correction);
            ++replayStats.redundantOrderRemovals;
            return true;
        }
        return false;
    }
    if (order.kind == EventKind::add) {
        correctionRemovals.erase(order.order_id);
    }
    tradeReconciler.observe(order);
    ++replayStats.orderEventsApplied;
    return true;
}

bool processTrade(OrderBook& orderBook, TradeReconciler& tradeReconciler,
                  ReplayStats& replayStats,
                  std::unordered_map<OrderId, Side, OrderIdHash>& correctionRemovals,
                  const TradeEvent& trade) {
    ++replayStats.tradeEventsRead;
    const auto corrections = tradeReconciler.reconcile(trade);
    replayStats.correctionsGenerated += corrections.size();

    for (const OrderEvent& correction : corrections) {
        const auto result = orderBook.apply(correction);
        if (!result.hasValue()) {
            return false;
        }
        if (correction.kind == EventKind::remove) {
            correctionRemovals.insert_or_assign(correction.order_id, correction.side);
        }
        ++replayStats.correctionsApplied;
    }
    return true;
}

}  // namespace

Result<ReplayResult, ReplayError> Replay::replay(BookSnapshot seed, const std::vector<OrderEvent>& orderEvents,
                                                    const std::vector<TradeEvent>& tradeEvents, std::uint64_t cutoffMicros) 
{
    if (!isTimeOrdered(orderEvents)) {
        return Result<ReplayResult, ReplayError>::failure(ReplayError::order_input_not_time_ordered);
    }
    if (!isTimeOrdered(tradeEvents)) {
        return Result<ReplayResult, ReplayError>::failure(ReplayError::trade_input_not_time_ordered);
    }

    const std::uint64_t seedTimestamp = seed.microtimestamp;
    TradeReconciler tradeReconciler;
    auto seeded = bootstrap(std::move(seed), &tradeReconciler);
    if (!seeded.hasValue()) {
        return Result<ReplayResult, ReplayError>::failure(ReplayError::bootstrap_failure);
    }
    OrderBook orderBook = std::move(*seeded.valueIf());

    ReplayStats replayStats{};
    EventClassifier classifier;
    std::unordered_map<OrderId, Side, OrderIdHash> correctionRemovals;
    std::size_t orderPtr{};
    std::size_t tradePtr{};

    while (orderPtr < orderEvents.size() &&
           orderEvents.at(orderPtr).venue_timestamp_us <= seedTimestamp) {
        classifier.classify(orderEvents.at(orderPtr));
        ++orderPtr;
    }
    while (tradePtr < tradeEvents.size() &&
           tradeEvents.at(tradePtr).venue_timestamp_us <= seedTimestamp) {
        ++tradePtr;
    }

    const auto orderInWindow = [&] {
        return orderPtr < orderEvents.size() &&
               orderEvents.at(orderPtr).venue_timestamp_us <= cutoffMicros;
    };
    const auto tradeInWindow = [&] {
        return tradePtr < tradeEvents.size() &&
               tradeEvents.at(tradePtr).venue_timestamp_us <= cutoffMicros;
    };
    while (orderInWindow() || tradeInWindow()) {
        if (!orderInWindow()) {
            auto result = processTrade(orderBook, tradeReconciler, replayStats, correctionRemovals,
                                       tradeEvents.at(tradePtr));
            if (!result) {
                return Result<ReplayResult, ReplayError>::failure(
                    ReplayError::unexpected_correction_apply_failure);
            }
            ++tradePtr;
        } else if (!tradeInWindow() || orderEvents.at(orderPtr).venue_timestamp_us <= tradeEvents.at(tradePtr).venue_timestamp_us) {
            auto result = processOrder(orderBook, tradeReconciler, classifier, replayStats,
                                       correctionRemovals, orderEvents.at(orderPtr));
            if (!result) {
                return Result<ReplayResult, ReplayError>::failure(
                    ReplayError::unexpected_order_apply_failure);
            }
            ++orderPtr;
        } else {
            auto result = processTrade(orderBook, tradeReconciler, replayStats, correctionRemovals,
                                       tradeEvents.at(tradePtr));
            if (!result) {
                return Result<ReplayResult, ReplayError>::failure(
                    ReplayError::unexpected_correction_apply_failure);
            }
            ++tradePtr;
        }
    }

    orderBook.validate();
    return Result<ReplayResult, ReplayError>::success(
        ReplayResult{.book = std::move(orderBook), .stats = replayStats});
}

}  // namespace te::bitstamp
