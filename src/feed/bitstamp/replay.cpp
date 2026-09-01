#include <te/feed/bitstamp/bootstrap.hpp>
#include <te/feed/bitstamp/classifier.hpp>
#include <te/feed/bitstamp/replay.hpp>
#include <te/feed/merge_cursor.hpp>
#include <te/feed/trade_reconciler.hpp>
#include <unordered_map>
#include <utility>

namespace te::bitstamp {
namespace {

// FNV-1a over the event's semantic fields, never its memory layout: a padding byte or a field
// reorder must not change the fingerprint, or the digest stops being comparable across compilers
// and across a future optimized book implementation.
constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t mixDigest(std::uint64_t digest, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        digest ^= (value >> (byte * 8)) & 0xFFULL;
        digest *= kFnvPrime;
    }
    return digest;
}

std::uint64_t mixEvent(std::uint64_t digest, const OrderEvent& event) {
    digest = mixDigest(digest, event.venue_timestamp_us);
    digest = mixDigest(digest, event.order_id.value);
    digest = mixDigest(digest, static_cast<std::uint64_t>(event.price.ticks));
    digest = mixDigest(digest, static_cast<std::uint64_t>(event.quantity.units));
    digest = mixDigest(digest, static_cast<std::uint64_t>(event.side));
    digest = mixDigest(digest, static_cast<std::uint64_t>(event.kind));
    return digest;
}

bool isTimeOrderedOrder(const std::vector<CapturedOrderEvent>& events) {
    for (std::size_t index = 1; index < events.size(); ++index) {
        if (events.at(index).event.venue_timestamp_us < events.at(index - 1).event.venue_timestamp_us) {
            return false;
        }
    }
    return true;
}
bool isTimeOrderedtrade(const std::vector<CapturedTradeEvent>& events) {
    for (std::size_t index = 1; index < events.size(); ++index) {
        if (events.at(index).event.venue_timestamp_us < events.at(index - 1).event.venue_timestamp_us) {
            return false;
        }
    }
    return true;
}

bool processOrder(OrderBook& orderBook, TradeReconciler& tradeReconciler,
                  EventClassifier& classifier, ReplayStats& replayStats,
                  std::unordered_map<OrderId, Side, OrderIdHash>& correctionRemovals,
                  const CapturedOrderEvent& order) {
    ++replayStats.orderEventsRead;
    if (classifier.classify(order.event) != EventDisposition::apply_to_book) {
        return true;
    }

    const auto result = orderBook.apply(order.event);
    if (!result.hasValue()) {
        // Tolerate only a later raw remove for an order this replay can prove a trade correction
        // already removed. Other unknown removes and every failed modify remain real errors.
        const auto correction = correctionRemovals.find(order.event.order_id);
        if (order.event.kind == EventKind::remove && result.errorIf() != nullptr &&
            *result.errorIf() == ApplyError::unknown_order_id &&
            correction != correctionRemovals.end() && correction->second == order.event.side) {
            correctionRemovals.erase(correction);
            ++replayStats.redundantOrderRemovals;
            return true;
        }
        return false;
    }
    if (order.event.kind == EventKind::add) {
        // Order IDs may be reused; a new lifecycle invalidates any old removal tombstone.
        correctionRemovals.erase(order.event.order_id);
    }
    // Observe only successfully applied raw events so the shadow matches the real book.
    tradeReconciler.observe(order.event, order.amountTraded);
    replayStats.appliedEventDigest = mixEvent(replayStats.appliedEventDigest, order.event);
    ++replayStats.orderEventsApplied;
    return true;
}

bool processTrade(OrderBook& orderBook, TradeReconciler& tradeReconciler, ReplayStats& replayStats,
                  std::unordered_map<OrderId, Side, OrderIdHash>& correctionRemovals,
                  const CapturedTradeEvent& trade) {
    ++replayStats.tradeEventsRead;
    const auto corrections = tradeReconciler.reconcile(trade.event);
    replayStats.correctionsGenerated += corrections.size();

    for (const OrderEvent& correction : corrections) {
        const auto result = orderBook.apply(correction);
        if (!result.hasValue()) {
            return false;
        }
        if (correction.kind == EventKind::remove) {
            correctionRemovals.insert_or_assign(correction.order_id, correction.side);
        }
        // reconcile() already updates its shadow; observing this correction would apply it twice.
        replayStats.appliedEventDigest = mixEvent(replayStats.appliedEventDigest, correction);
        ++replayStats.correctionsApplied;
    }
    return true;
}

}  // namespace

Result<ReplayResult, ReplayError> Replay::replay(BookSnapshot seed,
                                                 const std::vector<CapturedOrderEvent>& orderEvents,
                                                 const std::vector<CapturedTradeEvent>& tradeEvents,
                                                 std::uint64_t cutoffMicros) {
    // The two-pointer merge is valid only when each input is ordered independently.
    if (!isTimeOrderedOrder(orderEvents)) {
        return Result<ReplayResult, ReplayError>::failure(
            ReplayError::order_input_not_time_ordered);
    }
    if (!isTimeOrderedtrade(tradeEvents)) {
        return Result<ReplayResult, ReplayError>::failure(
            ReplayError::trade_input_not_time_ordered);
    }

    const std::uint64_t seedTimestamp = seed.microtimestamp;
    TradeReconciler tradeReconciler;
    auto seeded = bootstrap(std::move(seed), &tradeReconciler);
    if (!seeded.hasValue()) {
        return Result<ReplayResult, ReplayError>::failure(ReplayError::bootstrap_failure);
    }
    OrderBook orderBook = std::move(*seeded.valueIf());

    ReplayStats replayStats{};
    // Seed the running digest properly rather than from zero, so a short tape still avalanches.
    replayStats.appliedEventDigest = kFnvOffsetBasis;
    EventClassifier classifier;
    std::unordered_map<OrderId, Side, OrderIdHash> correctionRemovals;
    // The merge order itself lives in MergeCursor so the tape writer applies the same tie-break
    // instead of reimplementing it. What stays here is what the cursor deliberately does not know
    // about: the classifier, the book and the reconciler.
    MergeCursor cursor{orderEvents, tradeEvents, seedTimestamp, cutoffMicros};

    // Pre-seed orders still train the stateful classifier (notably price-zero lifecycles), but
    // never touch the seeded book. Pre-seed trades are already represented by the snapshot.
    for (std::size_t index = 0; index < cursor.ordersBeforeSeed(); ++index) {
        classifier.classify(orderEvents.at(index).event);
        ++replayStats.orderEventsBeforeSeed;
    }
    replayStats.tradeEventsBeforeSeed = cursor.tradesBeforeSeed();

    while (const auto pick = cursor.next()) {
        if (pick->stream == MergedStream::order) {
            if (!processOrder(orderBook, tradeReconciler, classifier, replayStats,
                              correctionRemovals, orderEvents.at(pick->index))) {
                return Result<ReplayResult, ReplayError>::failure(
                    ReplayError::unexpected_order_apply_failure);
            }
        } else {
            if (!processTrade(orderBook, tradeReconciler, replayStats, correctionRemovals,
                              tradeEvents.at(pick->index))) {
                return Result<ReplayResult, ReplayError>::failure(
                    ReplayError::unexpected_correction_apply_failure);
            }
        }
    }

    // Whatever the merge left behind is past the cutoff. Counting it here is what makes
    // beforeSeed + read + afterCutoff == input size, so a silently skipped event is visible.
    replayStats.orderEventsAfterCutoff = cursor.ordersAfterCutoff();
    replayStats.tradeEventsAfterCutoff = cursor.tradesAfterCutoff();

    orderBook.validate();
    replayStats.reconciler = tradeReconciler.stats();
    return Result<ReplayResult, ReplayError>::success(
        ReplayResult{.book = std::move(orderBook), .stats = replayStats});
}

}  // namespace te::bitstamp
