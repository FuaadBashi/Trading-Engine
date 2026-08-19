#pragma once

#include <optional>
#include <type_traits>
#include <te/core/result.hpp>
#include <te/feed/events.hpp>
#include <map>
#include <te/book/price_level.hpp>
#include <unordered_map>

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

struct OrderLocator{
    Side side;
    Price price;
    OrderHandle order_pos;
};


class OrderBook {
public:
    OrderBook() = default;

    // OrderLocator stores iterators into this book's PriceLevel lists. A normal copy would copy
    // those iterators while copying the lists into different nodes, leaving the copied locators
    // referring to the original book. Moving transfers ownership of the existing nodes instead.
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = default;
    OrderBook& operator=(OrderBook&&) = default;

    // Never silently ignores an event; see ADR 0012 for the add/modify/remove rules.
    Result<ApplyOutcome, ApplyError> apply(const OrderEvent& orderEvent);
    void validate() const;

    // Empty side is absence, not a sentinel price 
    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;

    // "No level" and "zero resting quantity" share the same meaning for this aggregate query.
    Qty qtyAt(Side side, Price price) const;

    // Derived fresh from bids_/asks_ every call, never cached -- there's no expensive walk to
    // avoid here (std::map::size() is already O(1)), so there's nothing worth caching.
    std::size_t levelCount() const { return bids_.size() + asks_.size(); }

    private:
    std::map<Price, PriceLevel> bids_{};
    std::map<Price, PriceLevel> asks_{};
    std::unordered_map<OrderId, OrderLocator, OrderIdHash> orderIndex_{};
};

static_assert(!std::is_copy_constructible_v<OrderBook>,
              "OrderBook must not be copied because its OrderLocators contain iterators into its "
              "own PriceLevel lists.");
static_assert(!std::is_copy_assignable_v<OrderBook>,
              "OrderBook must not be copy-assigned because its OrderLocators contain iterators "
              "into its own PriceLevel lists.");
static_assert(std::is_move_constructible_v<OrderBook>,
              "OrderBook must remain movable so ownership can be transferred without copying "
              "iterator-bearing state.");
static_assert(std::is_move_assignable_v<OrderBook>,
              "OrderBook must remain move-assignable so ownership can be transferred without "
              "copying iterator-bearing state.");

}  // namespace te
