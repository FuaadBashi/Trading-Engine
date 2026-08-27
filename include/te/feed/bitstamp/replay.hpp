#pragma once

#include <cstddef>
#include <cstdint>
#include <te/book/order_book.hpp>
#include <te/core/result.hpp>
#include <te/feed/bitstamp/snapshot.hpp>
#include <te/feed/events.hpp>
#include <te/feed/trade_event.hpp>
#include <vector>

namespace te::bitstamp {

struct ReplayStats {
    std::size_t orderEventsRead{};
    std::size_t orderEventsApplied{};
    std::size_t tradeEventsRead{};
    std::size_t correctionsGenerated{};
    std::size_t correctionsApplied{};
    std::size_t redundantOrderRemovals{};
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
                                             const std::vector<OrderEvent>& orderEvents,
                                             const std::vector<TradeEvent>& tradeEvents,
                                             std::uint64_t cutoffMicros);
};

}  // namespace te::bitstamp
