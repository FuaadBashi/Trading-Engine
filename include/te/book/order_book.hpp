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
    level_quantity_overflow,
};

class OrderBook {
public:
    // Never silently ignores an event; see ADR 0012 for the add/modify/remove rules.
    Result<ApplyOutcome, ApplyError> apply(const OrderEvent& orderEvent);

    // Empty side is absence, not a sentinel price 
    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;

    // "No level" and "zero resting quantity" share the same meaning for this aggregate query.
    Qty qtyAt(Side side, Price price) const;

    // invariant: !bestBid() || !bestAsk() || *bestBid() < *bestAsk()
};

}  // namespace te
