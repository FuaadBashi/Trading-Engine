#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <te/book/order_book.hpp>
#include <te/core/result.hpp>
#include <te/feed/bitstamp/snapshot.hpp>
#include <te/feed/events.hpp>
#include <te/feed/trade_event.hpp>

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
    // Events at or before the seed timestamp are already represented by the snapshot and are
    // skipped. Events later than cutoffMicros are not replayed. Equal order/trade timestamps are
    // processed order first as the current provisional replay policy.
    Result<ReplayResult, ReplayError> replay( BookSnapshot seed, const std::vector<OrderEvent>& orderEvents, 
                                                const std::vector<TradeEvent>& tradeEvents, std::uint64_t cutoffMicros);
};

}  // namespace te::bitstamp
