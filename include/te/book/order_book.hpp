#pragma once

#include <optional>

#include <te/core/result.hpp>
#include <te/feed/events.hpp>

// Slice 2. Reference book. Interface and failure contract per ADR 0012.

namespace te {

/** @brief What applying an event changed, for telemetry without exposing internal storage. */
struct ApplyOutcome {
    bool createdLevel{};
    bool removedLevel{};
};

/** @brief Reason apply() could not accept an event. See ADR 0012 for the event-by-event rules. */
enum class ApplyError {
    duplicate_order_id,
    unknown_order_id,
    invalid_price,
    invalid_quantity,
    side_mismatch,
    price_mismatch,
    level_quantity_overflow,
};

class OrderBook {
public:
    // Never silently ignores an event; see ADR 0012 for add/modify/remove rules, including that
    // remove locates and erases the order's *stored* price/quantity, not the message's own.
    Result<ApplyOutcome, ApplyError> apply(const OrderEvent&);

    // Empty side is absence, not a sentinel price -- see ADR 0012.
    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;

    // "No level" and "zero resting quantity" share the same meaning for this aggregate query.
    Qty qtyAt(Side, Price) const;

    // invariant: !bestBid() || !bestAsk() || *bestBid() < *bestAsk()
};

}  // namespace te
