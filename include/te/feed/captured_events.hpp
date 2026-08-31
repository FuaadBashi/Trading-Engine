#pragma once

#include <cstdint>

#include "te/feed/events.hpp"
#include "te/feed/trade_event.hpp"

namespace te {

// A normalized event plus capture-only metadata needed for deterministic replay.
// Keeping these envelopes outside the disk loader lets replay consume events from
// any source that can preserve their original arrival order.
struct CapturedOrderEvent {
    OrderEvent event;
    Qty amountTraded;
    std::uint64_t captureOrdinal{};
};

struct CapturedTradeEvent {
    TradeEvent event;
    std::uint64_t captureOrdinal{};
};

}  // namespace te
