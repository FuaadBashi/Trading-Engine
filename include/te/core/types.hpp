#pragma once

#include <compare>
#include <cstdint>
#include <type_traits>

// Slice 1. Price (int64 ticks), Qty, OrderId, Side, Instrument.
// See ADR 0004 (int64 ticks, per-instrument scale) and ADR 0005 (uint64 order ids).
//
// These are distinct struct types, not aliases for the underlying integer. That is what
// stops a Qty being passed where a Price is expected -- an alias would compile silently.
// The scale is NOT encoded here: per ADR 0004 it lives in InstrumentSpec, because it is a
// property of the venue/instrument pair rather than of the type.

namespace te {

enum class Side : std::uint8_t {
    buy,
    sell,
};

static_assert(std::is_same_v<std::underlying_type_t<Side>, std::uint8_t>,
              "Side must retain its one-byte representation because normalized event and queue "
              "layouts depend on it.");
static_assert(std::is_trivially_copyable_v<Side>,
              "Side must remain a trivial value type for predictable, allocation-free use "
              "inside normalized events and fixed-capacity queues.");

// Price in integer ticks. For Bitstamp BTC/USD one tick is 0.01 USD, so 65168.69 is
// stored as 6'516'869. Never a double: see ADR 0004.
struct Price {
    std::int64_t ticks{};

    friend constexpr bool operator==(const Price&, const Price&) = default;

    friend constexpr auto operator<=>(const Price&, const Price&) = default;
};

static_assert(std::is_trivially_copyable_v<Price>,
              "Price must remain a trivial value type for predictable, allocation-free use "
              "inside normalized events and fixed-capacity queues; this does not define its "
              "binary serialization.");

// Quantity in integer units. For Bitstamp BTC/USD one unit is 0.00000001 BTC.
// Signed rather than unsigned so that a difference of two quantities is representable.
struct Qty {
    std::int64_t units{};

    friend constexpr bool operator==(const Qty&, const Qty&) = default;
};

static_assert(std::is_trivially_copyable_v<Qty>,
              "Qty must remain a trivial value type for predictable, allocation-free use "
              "inside normalized events and fixed-capacity queues; this does not define its "
              "binary serialization.");

// Venue order identifier. Bitstamp issues 16-digit decimal integers; parsed from the
// id_str field with overflow checking, never from the JSON number. See ADR 0005.
struct OrderId {
    std::uint64_t value{};

    friend constexpr bool operator==(const OrderId&, const OrderId&) = default;
};

static_assert(std::is_trivially_copyable_v<OrderId>,
              "OrderId must remain a trivial value type for predictable, allocation-free use "
              "inside normalized events and fixed-capacity queues; this does not define its "
              "binary serialization.");

}  // namespace te
