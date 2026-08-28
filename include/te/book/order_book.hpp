#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <te/book/price_level.hpp>
#include <te/core/result.hpp>
#include <te/feed/events.hpp>
#include <type_traits>
#include <unordered_map>

namespace te {

// Reports structural changes without exposing the book's containers.
struct ApplyOutcome {
    bool createdLevel{};
    bool removedLevel{};
};

enum class ApplyError {
    duplicate_order_id,
    unknown_order_id,
    invalid_price,
    invalid_quantity,
    side_mismatch,
    level_quantity_overflow,
};

// Non-owning index entry. The PriceLevel owns the order node; this stores its stable list handle.
struct OrderLocator {
    Side side;
    Price price;
    OrderHandle order_pos;
};

// Sparse reference L3 book: ordered maps own levels and orderIndex_ gives direct ID lookup.
// It is the correctness oracle; optimized books must reproduce its results (ADR 0007/0012).
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

    // Every rejected event has a reason; callers decide whether it means bad input or resync.
    Result<ApplyOutcome, ApplyError> apply(const OrderEvent& orderEvent);

    // Debug assertions for index, aggregate, and best-price invariants.
    void validate() const;

    // Empty side is absence, never a sentinel price.
    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;

    // "No level" and "zero resting quantity" share the same meaning for this aggregate query.
    Qty qtyAt(Side side, Price price) const;

    std::size_t levelCount() const { return bids_.size() + asks_.size(); }

    /**
     * @brief  Order-independent fingerprint of the book's resting state.
     *
     * @return A digest of every non-empty level as (side, price, aggregate quantity).
     *
     * @note   Hashes semantic content in sorted price order, never memory layout, so two books
     *         holding the same liquidity agree regardless of insertion order, allocator behaviour,
     *         struct padding or compiler. That is the property that makes it usable to compare a
     *         future optimized book against this reference one (plan v4 Stage 8).
     *
     * @note   Deliberately excludes order IDs and per-order queue position: this answers "is the
     *         same liquidity resting at the same prices", which is what a checkpoint snapshot can
     *         independently confirm. A snapshot cannot confirm queue order, so hashing it would
     *         produce a digest nothing external could ever verify.
     */
    std::uint64_t digest() const;

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
