#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace te {

// Explicit one-byte layout is part of the fixed-size event/record contract.
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

// Exact integer ticks; the venue/instrument scale lives in InstrumentSpec (ADR 0004).
// A strong type prevents accidentally mixing price with quantity.
struct Price {
    std::int64_t ticks{};

    friend constexpr bool operator==(const Price&, const Price&) = default;

    friend constexpr auto operator<=>(const Price&, const Price&) = default;
};
struct PriceHash {
    std::size_t operator()(Price price) const noexcept {
        return std::hash<std::int64_t>{}(price.ticks);
    }
};

static_assert(std::is_trivially_copyable_v<Price>,
              "Price must remain a trivial value type for predictable, allocation-free use "
              "inside normalized events and fixed-capacity queues; this does not define its "
              "binary serialization.");

// Signed integer units so quantity differences cannot wrap like unsigned subtraction.
struct Qty {
    std::int64_t units{};

    friend constexpr bool operator==(const Qty&, const Qty&) = default;
};

static_assert(std::is_trivially_copyable_v<Qty>,
              "Qty must remain a trivial value type for predictable, allocation-free use "
              "inside normalized events and fixed-capacity queues; this does not define its "
              "binary serialization.");

// Bitstamp IDs are parsed exactly from id_str; event-chain UUIDs are a different identifier.
struct OrderId {
    std::uint64_t value{};

    friend constexpr bool operator==(const OrderId&, const OrderId&) = default;
};

static_assert(std::is_trivially_copyable_v<OrderId>,
              "OrderId must remain a trivial value type for predictable, allocation-free use "
              "inside normalized events and fixed-capacity queues; this does not define its "
              "binary serialization.");

// Named hasher keeps unordered-container use explicit without weakening OrderId's strong type.
struct OrderIdHash {
    std::size_t operator()(const OrderId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

}  // namespace te
