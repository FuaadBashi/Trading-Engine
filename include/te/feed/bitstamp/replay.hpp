#pragma once

#include <cstddef>
#include <cstdint>
#include <te/book/order_book.hpp>
#include <te/core/result.hpp>
#include <te/feed/bitstamp/snapshot.hpp>
#include <te/feed/segment_loader.hpp>
#include <te/feed/events.hpp>
#include <te/feed/trade_event.hpp>
#include <vector>
#include "te/feed/trade_reconciler.hpp"

namespace te::bitstamp {

// Every input event lands in exactly one of beforeSeed / read / afterCutoff, so the three sum to
// the input size. Plan v4 §12 requires the controller to account for every input; without the two
// window counters a merge-loop bug that silently skipped events would leave no trace.
struct ReplayStats {
    std::size_t orderEventsBeforeSeed{};
    std::size_t orderEventsRead{};
    std::size_t orderEventsAfterCutoff{};
    std::size_t orderEventsApplied{};

    std::size_t tradeEventsBeforeSeed{};
    std::size_t tradeEventsRead{};
    std::size_t tradeEventsAfterCutoff{};

    std::size_t correctionsGenerated{};
    std::size_t correctionsApplied{};
    std::size_t redundantOrderRemovals{};
    ReconcilerStats reconciler {};

    // Order-independent fingerprint of every event applied to the book, raw and corrective alike.
    // Two runs that agree here processed the same tape; Stage 8 needs it to compare an optimized
    // book against this reference one.
    std::uint64_t appliedEventDigest{};

    std::size_t orderEventsAccountedFor() const {
        return orderEventsBeforeSeed + orderEventsRead + orderEventsAfterCutoff;
    }
    std::size_t tradeEventsAccountedFor() const {
        return tradeEventsBeforeSeed + tradeEventsRead + tradeEventsAfterCutoff;
    }
};

enum class ReplayError {
    bootstrap_failure,
    order_input_not_time_ordered,
    trade_input_not_time_ordered,
    unexpected_order_apply_failure,
    unexpected_correction_apply_failure,
};

struct ReplayResult {
    OrderBook book;
    ReplayStats stats;
};

class Replay {
public:
    // Bootstraps internally, merges two individually time-ordered streams, and applies only
    // seedTimestamp < eventTimestamp <= cutoff. Exact ties process the order first.
    Result<ReplayResult, ReplayError> replay(BookSnapshot seed,
                                             const std::vector<CapturedOrderEvent>& orderEvents,
                                             const std::vector<CapturedTradeEvent>& tradeEvents,
                                             std::uint64_t cutoffMicros);
};

}  // namespace te::bitstamp
