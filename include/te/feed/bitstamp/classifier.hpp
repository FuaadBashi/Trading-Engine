#pragma once

#include <cstddef>
#include <unordered_set>

#include <te/core/types.hpp>
#include <te/feed/events.hpp>

// Slice 2. The venue-aware boundary between Bitstamp decoding and the venue-neutral OrderBook.

namespace te::bitstamp {

/**
 * @brief  What a driver should do with one decoded event.
 */
enum class EventDisposition {
    /**
     * @brief  Hand this event to OrderBook.
     *
     * @note   Deliberately named for the action, not for a claim about the order. This does
     *         **not** assert the event represents stable resting liquidity -- only that the
     *         classifier could not positively identify it as something else. Bitstamp's
     *         live_orders channel also exposes marketable order lifecycles that may cross the
     *         visible book and rest for a very short time, or never rest at all. Those still
     *         arrive here, because distinguishing them needs joined order/trade evidence this
     *         classifier does not have. Treat this as "not excluded", not as "proven resting".
     */
    apply_to_book,

    /**
     * @brief  A price-zero lifecycle: never a resting price level, and safe to ignore.
     *
     * @note   The corpus contains order_created events at price zero. A price of zero is not a
     *         real book price, so inserting one would invent a level at the bottom of the bid
     *         ladder that no participant can trade against. The whole lifecycle is skipped --
     *         the create, and the later change/delete carrying the same id.
     */
    zero_price_lifecycle,
};

/**
 * @brief  Counts of what the classifier decided, for audit.
 *
 * @note   Same reasoning as RecorderStats in Slice 1: a run that quietly discarded a large
 *         fraction of its input looks identical to a healthy one unless the discards are
 *         counted. Every event the classifier declines to pass on is counted here, so the
 *         number of ignored lifecycles is always recoverable rather than invisible.
 */
struct ClassifierStats {
    /** @brief Events dispatched to the book. */
    std::size_t appliedToBook{};

    /** @brief Events skipped because they belong to a price-zero lifecycle. */
    std::size_t zeroPriceLifecycle{};
};

/**
 * @brief  Decides whether a decoded Bitstamp event belongs in the order book.
 *
 * @note   Exists so that OrderBook never has to understand Bitstamp. Venue quirks -- price-zero
 *         market-order lifecycles today, order_subtype semantics later -- are recognised here
 *         and turned into a venue-neutral disposition. The book stays a pure function of the
 *         events it is handed, which is what keeps it independently testable.
 *
 * @note   Stateful by necessity, not by preference. A price-zero order is recognisable from its
 *         `order_created`, but its later `order_changed`/`order_deleted` carry a normal-looking
 *         price and are indistinguishable from real book traffic without remembering the id.
 *         Only the id is retained -- never the order itself -- and it is released when the
 *         lifecycle ends with a remove.
 *
 * @note   OrderEvent is deliberately unchanged by this component. Bitstamp's `order_subtype`
 *         field is not currently decoded, and adding it would alter OrderEvent's layout, which
 *         is embedded in the versioned on-disk Record. What the durable v3 format should store
 *         about subtype is an open question to settle from joined order/trade data (ADR 0011),
 *         not one to answer by widening the in-memory domain model first.
 */
class EventClassifier {
public:
    /**
     * @brief  Classifies one decoded event, updating internal lifecycle state and counters.
     *
     * @param  event A decoded, venue-neutral order event.
     *
     * @return Whether the caller should apply this event to the book, or skip it.
     *
     * @note   Must be called on every event in stream order, exactly once. Skipping events, or
     *         replaying one twice, corrupts the tracked lifecycle set and can let a price-zero
     *         change/delete reach the book.
     */
    EventDisposition classify(const OrderEvent& event);

    /** @brief Counts of every decision made so far. */
    const ClassifierStats& stats() const { return stats_; }

    /**
     * @brief  How many price-zero lifecycles are currently open.
     *
     * @note   Expected to return to zero over a healthy capture: each tracked id is released by
     *         its own remove. A count that only grows suggests removes are being missed, which
     *         is worth surfacing rather than leaving to accumulate silently.
     */
    std::size_t openZeroPriceLifecycles() const { return zeroPriceOrders_.size(); }

private:
    std::unordered_set<OrderId, OrderIdHash> zeroPriceOrders_;
    ClassifierStats stats_{};
};

}  // namespace te::bitstamp
