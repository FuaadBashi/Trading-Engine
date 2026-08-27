#pragma once

#include <te/core/types.hpp>

namespace te {

// One execution names both counterpart order IDs; either or both may have been resting.
struct TradeEvent {
    std::uint64_t venue_timestamp_us{};
    OrderId buy_order_id{};
    OrderId sell_order_id{};
    Qty quantity{};
};

}  // namespace te
