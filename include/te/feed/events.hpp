#pragma once

#include <cstdint>
#include <type_traits>

#include "te/core/types.hpp"

// Slice 1. Normalised market events. Trivially copyable, fixed size, no std::string.

namespace te {

/**
 * @brief  Normalized lifecycle operation for one resting L3 order.
 *
 * @note   Venue-neutral: Bitstamp's order_created/order_changed/order_deleted are translated
 *         into these at the decoder boundary, so the book never sees venue vocabulary.
 *
 * @note   Fixed to a one-byte representation because normalized event and fixed-capacity
 *         queue layouts depend on its size.
 */
enum class EventKind : std::uint8_t {
    /** @brief Insert a previously absent order. */
    add,

    /** @brief Replace an existing order's reported remaining quantity. */
    modify,

    /** @brief Remove an existing order, located by OrderId. */
    remove,
};

static_assert(std::is_same_v<std::underlying_type_t<EventKind>, std::uint8_t>,
              "EventKind must retain its one-byte representation because normalized event and "
              "fixed-capacity queue layouts depend on it.");

/**
 * @brief  Venue-neutral L3 order event consumed by the order book.
 *
 * @note   Contains only fixed-size, trivially-copyable value types. Raw JSON fields and
 *         venue-specific strings belong at the decoder boundary, not in the book's hot path.
 */
struct OrderEvent {
    /** @brief Exchange-supplied timestamp, normalized to microseconds since the Unix epoch. */
    std::uint64_t venue_timestamp_us{};

    /** @brief Stable venue order identifier used to locate the resting order. */
    OrderId order_id{};

    /** @brief Exact integer-tick price of the order. */
    Price price{};

    /**
     * @brief  Resulting venue-reported resting quantity; never a delta.
     *
     * @note   Interpretation depends on `kind`:
     *         - add: quantity inserted into the book;
     *         - modify: new remaining quantity, replacing the previous quantity;
     *         - remove: reported deletion quantity, retained for audit only. Removal is by
     *           OrderId and must not subtract this value from a price level.
     */
    Qty quantity{};

    /** @brief Buy or sell side on which the order rests. */
    Side side{};

    /** @brief Lifecycle operation represented by this event. */
    EventKind kind{};
};

// After the closing brace, not inside it: a class is incomplete until then, and
// is_trivially_copyable requires a complete type.
static_assert(std::is_trivially_copyable_v<OrderEvent>,
              "OrderEvent must remain trivially copyable for fixed-capacity queue transport.");

}  // namespace te
