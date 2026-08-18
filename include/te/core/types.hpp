#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

// Slice 1. Price (int64 ticks), Qty, OrderId, Side.
// See ADR 0004 (int64 ticks, per-instrument scale) and ADR 0005 (uint64 order ids).

namespace te {

/**
 * @brief  Side of the book an order rests on.
 *
 * @note   Fixed to a one-byte representation because normalized event and fixed-capacity
 *         queue layouts depend on its size.
 */
enum class Side : std::uint8_t {
    /** @brief Bid side: an order to buy. */
    buy,

    /** @brief Ask side: an order to sell. */
    sell,
};

static_assert(std::is_same_v<std::underlying_type_t<Side>, std::uint8_t>,
              "Side must retain its one-byte representation because normalized event and queue "
              "layouts depend on it.");
static_assert(std::is_trivially_copyable_v<Side>,
              "Side must remain a trivial value type for predictable, allocation-free use "
              "inside normalized events and fixed-capacity queues.");

/**
 * @brief  A price, in whole integer ticks.
 *
 * @note   For Bitstamp BTC/USD one tick is 0.01 USD, so 65168.69 is stored as 6'516'869.
 *         Never a double: binary floating point cannot represent most decimal prices
 *         exactly, and the error compounds through accumulated PnL. See ADR 0004.
 *
 * @note   The tick scale is deliberately not encoded in this type. Per ADR 0004 it lives in
 *         InstrumentSpec, because it is a property of the venue/instrument pair rather than
 *         of the price itself.
 *
 * @note   A distinct struct rather than an alias for std::int64_t: an alias would let a Qty
 *         be passed where a Price is expected, and would compile silently.
 */
struct Price {
    /** @brief The price in ticks. */
    std::int64_t ticks{};

    friend constexpr bool operator==(const Price&, const Price&) = default;

    friend constexpr auto operator<=>(const Price&, const Price&) = default;
};

static_assert(std::is_trivially_copyable_v<Price>,
              "Price must remain a trivial value type for predictable, allocation-free use "
              "inside normalized events and fixed-capacity queues; this does not define its "
              "binary serialization.");

/**
 * @brief  A quantity, in whole integer units.
 *
 * @note   For Bitstamp BTC/USD one unit is 0.00000001 BTC.
 *
 * @note   Signed rather than unsigned so that the difference of two quantities is
 *         representable. Unsigned subtraction wraps to a huge positive value instead of
 *         going negative, and that wraparound is well-defined, so no sanitizer would catch
 *         it.
 */
struct Qty {
    /** @brief The quantity in units. */
    std::int64_t units{};

    friend constexpr bool operator==(const Qty&, const Qty&) = default;
};

static_assert(std::is_trivially_copyable_v<Qty>,
              "Qty must remain a trivial value type for predictable, allocation-free use "
              "inside normalized events and fixed-capacity queues; this does not define its "
              "binary serialization.");

/**
 * @brief  Venue-assigned identifier for one resting order.
 *
 * @note   Bitstamp issues 16-digit decimal integers. Parsed from the `id_str` string field
 *         with overflow checking, never from the JSON number, because generic JSON pipelines
 *         coerce numbers to double and lose exact integer representation above 2^53. See
 *         ADR 0005.
 *
 * @note   Unsigned, unlike Price and Qty: an id is an opaque label that is only ever
 *         compared for equality, never subtracted, so there is no wraparound hazard and the
 *         full 64-bit positive range is usable.
 */
struct OrderId {
    /** @brief The venue's numeric order identifier. */
    std::uint64_t value{};

    friend constexpr bool operator==(const OrderId&, const OrderId&) = default;
};

static_assert(std::is_trivially_copyable_v<OrderId>,
              "OrderId must remain a trivial value type for predictable, allocation-free use "
              "inside normalized events and fixed-capacity queues; this does not define its "
              "binary serialization.");

/**
 * @brief  Hashes an OrderId, for use as an unordered_map/unordered_set key.
 *
 * @note   OrderId has equality but no hash of its own, so a std::unordered_map keyed on it
 *         will not compile without one. Supplied as a named type rather than a
 *         std::hash<OrderId> specialisation so that every hashed container states its hashing
 *         explicitly at the point of use, matching this codebase's preference for supplied
 *         over inferred (compare InstrumentSpec's scales, which are passed in rather than
 *         guessed). A std::hash specialisation would also be correct and is the more common
 *         idiom; it was not chosen only because it hides the decision at the call site.
 *
 * @note   Forwards to std::hash<std::uint64_t> rather than inventing a mixing function.
 *         Venue ids are already well distributed, and a hand-rolled hash here would be
 *         unverified work on a path that does not need it.
 */
struct OrderIdHash {
    std::size_t operator()(const OrderId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

}  // namespace te
