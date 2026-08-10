#pragma once

#include <cstdint>
#include <type_traits>

#include "te/core/types.hpp"

// Slice 1. Normalised market events. Trivially copyable, fixed size, no std::string.

namespace te {

/// Normalized lifecycle operation for one resting L3 order.
enum class EventKind : std::uint8_t {
    /// Insert a previously absent order.
    add,

    /// Replace an existing order's reported remaining quantity.
    modify,

    /// Remove an existing order by OrderId.
    remove,
};

static_assert(std::is_same_v<std::underlying_type_t<EventKind>, std::uint8_t>,
              "EventKind must retain its one-byte representation because normalized event and "
              "fixed-capacity queue layouts depend on it.");

/// Venue-neutral L3 order event consumed by the order book.
///
/// It intentionally contains only fixed-size, trivially-copyable value types. Raw JSON fields and
/// venue-specific strings belong at the decoder boundary, not in the book's hot path.
struct OrderEvent {
    /// Exchange-supplied timestamp, normalized to microseconds since the Unix epoch.
    std::uint64_t venue_timestamp_us{};

    /// Stable venue order identifier used to locate the resting order.
    OrderId order_id{};

    /// Exact integer-tick price of the order.
    Price price{};

    /// Resulting venue-reported resting quantity, never a quantity delta:
    /// - add: quantity inserted into the book;
    /// - modify: new remaining quantity replacing the previous quantity;
    /// - remove: reported deletion quantity, retained for audit only. Removal is by OrderId and
    ///   must not subtract this value from a price level.
    Qty quantity{};

    /// Buy or sell side on which the order rests.
    Side side{};

    /// Lifecycle operation represented by this event.
    EventKind kind{};
};

// After the closing brace, not inside it: a class is incomplete until then, and
// is_trivially_copyable requires a complete type.
static_assert(std::is_trivially_copyable_v<OrderEvent>,
              "OrderEvent must remain trivially copyable for fixed-capacity queue transport.");

}  // namespace te
