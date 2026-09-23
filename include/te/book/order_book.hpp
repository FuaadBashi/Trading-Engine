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

// Reasons an event was rejected. Every value here is BAD MARKET INPUT (ADR 0012). Heap exhaustion
// is deliberately absent: ADR 0015 makes it fatal to the process, so it propagates as an exception
// rather than becoming an ApplyError a caller might mistake for a recoverable bad event.
enum class ApplyError {
    duplicate_order_id,
    unknown_order_id,
    invalid_price,
    invalid_quantity,
    side_mismatch,
    level_quantity_overflow,
    invalid_side,
    invalid_event_kind,
};

// Observable top-of-book shape. This reports market state, not whether the feed is trustworthy.
enum class MarketShape {
    empty,
    one_sided,
    locked,
    crossed,
    open,
};

// Non-owning index entry. The PriceLevel owns the order node; this stores its stable list handle.
struct OrderLocator {
    Side side;
    Price price;
    OrderHandle order_pos;
};

// Sparse reference L3 book: ordered maps own levels and orderIndex_ gives direct ID lookup.
// It is the correctness oracle; optimized books must reproduce its results (ADR 0007/0012).
// Bump when digest()'s algorithm or constants change; recorded digests are only comparable
// within one version.
inline constexpr std::uint32_t kBookDigestVersion = 1;

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
    [[nodiscard]] Result<ApplyOutcome, ApplyError> apply(const OrderEvent& orderEvent);

    // Debug-only: every check inside is assert(), which NDEBUG (release builds) compiles out
    // entirely. Calling this from code that also runs in release still walks the whole book for
    // zero effect -- guard the call site with #ifndef NDEBUG, as order_book.cpp's own apply()
    // does, rather than relying on this function to protect itself.
    void validateStructure() const;

    // Empty side is absence, never a sentinel price.
    [[nodiscard]] std::optional<Price> bestBid() const noexcept;
    [[nodiscard]] std::optional<Price> bestAsk() const noexcept;

    // "No level" and "zero resting quantity" share the same meaning for this aggregate query.
    [[nodiscard]] Qty qtyAt(Side side, Price price) const noexcept;

    [[nodiscard]] std::size_t levelCount() const noexcept { return bids_.size() + asks_.size(); }

    // Classifies the visible best prices without deciding whether trading is permitted.
    [[nodiscard]] MarketShape marketShape() const noexcept;

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
    [[nodiscard]] std::uint64_t digest() const noexcept;

private:
    Result<ApplyOutcome, ApplyError> applyAdd(const OrderEvent& orderEvent);
    Result<ApplyOutcome, ApplyError> applyModify(const OrderEvent& orderEvent);
    Result<ApplyOutcome, ApplyError> applyRemove(const OrderEvent& orderEvent);

    std::map<Price, PriceLevel> bids_{};
    std::map<Price, PriceLevel> asks_{};
    std::unordered_map<OrderId, OrderLocator, OrderIdHash> orderIndex_{};
    // Present in every build so the layout never differs; used only when NDEBUG is off.
    [[maybe_unused]] std::uint64_t debugAppliedEvents_{};
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
