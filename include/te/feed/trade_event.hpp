#pragma once

#include <te/core/types.hpp>

namespace te {

/** @brief A completed trade: two orders matched. Venue-neutral, same spirit as OrderEvent. */
struct TradeEvent {
    std::uint64_t venue_timestamp_us{};
    OrderId buy_order_id{};
    OrderId sell_order_id{};
    Qty quantity{};
};

}  // namespace te
