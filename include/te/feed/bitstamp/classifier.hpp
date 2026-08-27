#pragma once

#include <cstddef>
#include <te/core/types.hpp>
#include <te/feed/events.hpp>
#include <unordered_set>

namespace te::bitstamp {

enum class EventDisposition {
    // Means "not excluded", not proof that the order rested for a meaningful duration.
    apply_to_book,

    zero_price_lifecycle,
};

// Every decision is counted so filtering cannot silently shorten a replay.
struct ClassifierStats {
    std::size_t appliedToBook{};
    std::size_t zeroPriceLifecycle{};
};




// Venue-specific filter kept outside OrderBook. It remembers price-zero IDs because their later
// change/delete messages carry plausible prices and cannot otherwise be recognized.
class EventClassifier {
public:
    // Must see every order event exactly once and in stream order.
    EventDisposition classify(const OrderEvent& event);

    const ClassifierStats& stats() const { return stats_; }

    // A growing value suggests lifecycle-ending removes are missing.
    std::size_t openZeroPriceLifecycles() const { return zeroPriceOrders_.size(); }

private:
    std::unordered_set<OrderId, OrderIdHash> zeroPriceOrders_;
    ClassifierStats stats_{};
};

}  // namespace te::bitstamp
