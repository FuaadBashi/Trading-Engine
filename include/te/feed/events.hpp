#pragma once

#include <cstdint>
#include <type_traits>

#include "te/core/types.hpp"

namespace te {

// Venue-specific lifecycle names are normalized at the decoder boundary.
enum class EventKind : std::uint8_t {
    add,
    modify,
    remove,
};

static_assert(std::is_same_v<std::underlying_type_t<EventKind>, std::uint8_t>,
              "EventKind must retain its one-byte representation because normalized event and "
              "fixed-capacity queue layouts depend on it.");

// Fixed-size, venue-neutral event consumed by OrderBook and durable-record code.
struct OrderEvent {
    std::uint64_t venue_timestamp_us{};
    OrderId order_id{};
    Price price{};

    // Resulting resting quantity, never a delta. Remove locates by ID and ignores this value.
    Qty quantity{};
    Side side{};
    EventKind kind{};
};

static_assert(std::is_trivially_copyable_v<OrderEvent>,
              "OrderEvent must remain trivially copyable for fixed-capacity queue transport.");

}  // namespace te
